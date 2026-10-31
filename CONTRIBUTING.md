# Contributing

Bug reports, build questions and fixes are welcome.

## Before you start

- **Small fixes** (typos, doc corrections, clear bugs): open a pull request directly.
- **Anything larger** (new features, hardware changes, changes to fault handling): open an issue first so the approach can be agreed before you put in the work.
- **Hardware changes:** the EasyEDA Pro project in `hardware/` is the source of truth. Include updated schematic and layout PDFs with any change to it.
- **Firmware changes:** build with the pinned platform in `firmware/platformio.ini` and describe how you tested on hardware.

## Licensing of contributions

By submitting a contribution, you agree that it is licensed under the same license as the part of the repository it changes:

| Part | License |
|---|---|
| Firmware (`firmware/`, `tools/`) | GPL-3.0-or-later |
| Hardware (`hardware/`) | CERN-OHL-S-2.0 |
| Documentation (`docs/`, READMEs) | CC BY-SA 4.0 |

Only submit work you have the right to license on these terms.

## Safety

Do not submit changes that weaken the protection design (fuses, high-side switching, gate clamps, current sensing or fault latching) without explaining the reason in the pull request. Read the safety and liability notice in the [README](README.md) before building or testing.
