# Third-party software

| Component | Version | Licence | Used in |
|---|---|---|---|
| Catch2 | 3.7.1 | BSL-1.0 | tests |
| nlohmann/json | 3.11.3 | MIT | core |
| JUCE | 8.0.4 | AGPLv3 | audio device and VST3 hosting (lpc-cli, the UI, lpc-plugin-scanner) |

- **VST3 SDK** (bundled with JUCE 8.0.4): GPLv3 option, compatible with AGPLv3. VST is a trademark of Steinberg Media Technologies GmbH.
- **Qt 6** (Quick, Quick Controls 2): LGPLv3, linked dynamically. Used by the UI (`ui/`), installed with aqtinstall.
- **Inter** font: SIL Open Font License 1.1, `assets/fonts/LICENSE.txt`.
