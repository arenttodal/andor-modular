ANDOR Modular test build for macOS (universal: Apple Silicon and Intel, macOS 11 or later)

1. Unzip this folder.
2. Double-click install.command. If macOS refuses to open it, right-click it, choose
   Open, then Open again. Or run in Terminal:  bash install.command
3. Restart your DAW.
   - Logic: Instrument slot > AU Instruments > OSP > ANDOR Modular
   - Ableton Live / Reaper: rescan plug-ins, then use the VST3 or AU "ANDOR Modular"
   - Or open ~/Applications/ANDOR Modular.app (standalone, choose your audio device in Options)

The twelve factory patches are in the preset bar (click the patch's name, or use the arrows).

This build is not signed or notarised (that needs a Developer ID). install.command
removes the download quarantine so macOS lets it load. It installs for your user only and
needs no administrator password.

Uninstall: delete "ANDOR Modular.component" from ~/Library/Audio/Plug-Ins/Components,
"ANDOR Modular.vst3" from ~/Library/Audio/Plug-Ins/VST3, "ANDOR Modular.app" from
~/Applications, and (if you like) your patches and the factory recordings in
~/Library/ANDOR/Modular.
