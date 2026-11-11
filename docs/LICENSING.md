# Licensing report

| Component | Licence | Use | Distribution note |
|---|---|---|---|
| ANDOR Modular, ANDOR OSP, ANDOR Analog code | proprietary (the owner's) | everything | — |
| JUCE 8.0.9 (fetched by OSP's CMake) | AGPLv3 or commercial JUCE licence | plugin, product tests, OSP I/O | **a commercial release needs the owner's JUCE licence** (or AGPL compliance); prototype only |
| VST3 SDK (bundled in JUCE) | per its bundled licence (check the SDK version in the JUCE tag at release) | plugin format, hosting | verify at release |
| Catch2 v3.9.1 | BSL-1.0 | tests only | not shipped |
| Signalsmith Stretch 1.4.0 (OSP dependency) | MIT | linked into osp_dsp (OSP's offline pitch engines) | keep its notice |
| Inter, Outfit typefaces | SIL Open Font License 1.1 (`external/osp/shared/andor-ui/Resources/fonts/*-OFL.txt`) | embedded UI fonts | ship the OFL texts with the product |
| Factory recordings (`presets/factory/samples`) | ANDOR's own, synthesised by `tools/make-samples` | factory patches | free of third-party rights |
| Factory convolution impulses | ANDOR Analog's synthesised impulses | CONVOLUTION | ANDOR's own |
| Hosted plugins (e.g. Kontakt) | the user's licence | EXTERNAL INSTRUMENT loads what the user owns | Modular never bundles third-party plugins or libraries; patches store only the file path and the plugin's own state |

No new commercial dependency was added. No third-party sample library is embedded in any preset.
