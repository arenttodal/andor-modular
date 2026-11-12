#!/usr/bin/env bash
# ANDOR Modular: the macOS installer (one .pkg: AU, VST3, standalone app). After ANDOR Analog's
# packaging/macos/build-installer.sh: signed and notarised when credentials are present,
# otherwise an UNSIGNED but otherwise identical package (ad-hoc signed bundles), for testing.
#
#   packaging/macos/build-installer.sh <artefacts dir> <version> <out dir>
#
#   <artefacts dir>  build/product/ANDOR_Modular_artefacts/Release
#
# Signing (both identities, or none):
#   MODULAR_APP_IDENTITY        "Developer ID Application: <Name> (<TEAMID>)"
#   MODULAR_INSTALLER_IDENTITY  "Developer ID Installer: <Name> (<TEAMID>)"
#   MODULAR_NOTARY_KEY / MODULAR_NOTARY_KEY_ID / MODULAR_NOTARY_ISSUER   an App Store Connect API key
# The script never prints secrets. Identifiers never change: com.osp.andormodular.*.
set -euo pipefail

art="${1:?artefacts dir}"; version="${2:?version}"; out="${3:?out dir}"
name="ANDOR Modular"
for b in "$art/AU/$name.component" "$art/VST3/$name.vst3" "$art/Standalone/$name.app"; do
    [ -d "$b" ] || { echo "missing $b" >&2; exit 2; }
done

signed=0
if [ -n "${MODULAR_APP_IDENTITY:-}" ] && [ -n "${MODULAR_INSTALLER_IDENTITY:-}" ]; then signed=1; fi

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/au" "$stage/vst3" "$stage/app" "$out"
cp -R "$art/AU/$name.component" "$stage/au/"
cp -R "$art/VST3/$name.vst3" "$stage/vst3/"
cp -R "$art/Standalone/$name.app" "$stage/app/"
xattr -cr "$stage" 2>/dev/null || true

# Developer ID with the hardened runtime and a secure timestamp; without an identity an ad-hoc
# signature, so the bundles are well formed (Apple silicon refuses unsigned code) but not trusted.
for b in "$stage/au/$name.component" "$stage/vst3/$name.vst3" "$stage/app/$name.app"; do
    if [ "$signed" = 1 ]; then
        codesign --force --deep --options runtime --timestamp --sign "$MODULAR_APP_IDENTITY" "$b"
    else
        codesign --force --deep --sign - "$b"
    fi
    codesign --verify --deep --strict "$b"
done

pkgbuild --root "$stage/au"   --install-location "/Library/Audio/Plug-Ins/Components" --identifier com.osp.andormodular.au   --version "$version" "$stage/au.pkg"
pkgbuild --root "$stage/vst3" --install-location "/Library/Audio/Plug-Ins/VST3"       --identifier com.osp.andormodular.vst3 --version "$version" "$stage/vst3.pkg"
# (not relocatable: the app goes to /Applications, never onto another copy found elsewhere)
pkgbuild --analyze --root "$stage/app" "$stage/app.plist"
/usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$stage/app.plist"
pkgbuild --root "$stage/app"  --install-location "/Applications" --component-plist "$stage/app.plist" --identifier com.osp.andormodular.app --version "$version" "$stage/app.pkg"

cat > "$stage/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>ANDOR Modular $version</title>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <os-version min="11.0"/>
    <choices-outline>
        <line choice="au"/><line choice="vst3"/><line choice="app"/>
    </choices-outline>
    <choice id="au" title="Audio Unit (Logic Pro, GarageBand, Ableton Live)"><pkg-ref id="com.osp.andormodular.au"/></choice>
    <choice id="vst3" title="VST3 (Ableton Live, Cubase, REAPER, Bitwig, Studio One)"><pkg-ref id="com.osp.andormodular.vst3"/></choice>
    <choice id="app" title="Standalone application"><pkg-ref id="com.osp.andormodular.app"/></choice>
    <pkg-ref id="com.osp.andormodular.au">au.pkg</pkg-ref>
    <pkg-ref id="com.osp.andormodular.vst3">vst3.pkg</pkg-ref>
    <pkg-ref id="com.osp.andormodular.app">app.pkg</pkg-ref>
</installer-gui-script>
XML

suffix=""; [ "$signed" = 1 ] || suffix="-UNSIGNED"
pkg="$out/ANDOR-Modular-$version-macOS$suffix.pkg"
if [ "$signed" = 1 ]; then
    productbuild --distribution "$stage/distribution.xml" --package-path "$stage" --sign "$MODULAR_INSTALLER_IDENTITY" --timestamp "$pkg"
else
    productbuild --distribution "$stage/distribution.xml" --package-path "$stage" "$pkg"
fi

if [ "$signed" = 1 ]; then
    if [ -n "${MODULAR_NOTARY_KEY:-}" ]; then
        key="$stage/notary.p8"; printf '%s' "$MODULAR_NOTARY_KEY" > "$key"
        xcrun notarytool submit "$pkg" --key "$key" --key-id "$MODULAR_NOTARY_KEY_ID" --issuer "$MODULAR_NOTARY_ISSUER" --wait
    else
        echo "signed but NOT notarised (no notary credentials)" >&2
    fi
    xcrun stapler staple "$pkg" || echo "stapling failed (not notarised?)" >&2
    spctl --assess --type install -vv "$pkg" || true
fi
pkgutil --check-signature "$pkg" || true
( cd "$out" && shasum -a 256 "$(basename "$pkg")" > "$(basename "$pkg").sha256" )
echo "installer: $pkg (signed=$signed)"
