# Hardware

Design files for the controller PCB (rev 0.2) and a reference panel design. Assembly steps are in the [Build Guide](../docs/BUILD.md).

> [!WARNING]
> **Safety and liability**
>
> Building and using this project involves soldering, a mains-powered 24 V supply, and currents of several amps. Mistakes can cause electric shock, fire, burns, or damage to equipment. Build it only if you are competent to do this kind of work, follow the safety instructions for your power supply and tools, and comply with local electrical codes. Never work on the controller or panels while they are powered.
>
> By building or using this project, you acknowledge these risks and voluntarily assume all risk arising from it, including the risk of personal injury or death and of damage to or loss of property. The design, firmware, and documentation are provided "as is" and "with all faults," without warranty of any kind, express or implied. The authors accept no liability for personal injury, death, or property damage or loss arising from their use. This notice is in addition to the warranty disclaimers and limitations of liability in this repository's licenses.

```
hardware/
  EasyEda Project/            EasyEDA Pro project (schematic and PCB source)
  schematics/                 Schematic and PCB layout PDFs
  gerber/                     JLCPCB order package: gerbers, BOM, pick-and-place
  bom/                        Hand-assembly BOM (through-hole and off-board parts)
  mechanical/
    enclosure/pcb/            Controller enclosure (STL)
    panels/reference-design/  Reference panel design
```

STEP models of the controller housing and the board are attached to the GitHub release.

## Ordering

| File | Contents |
|---|---|
| `gerber/CCTRL_rev0.2_gerber_*.zip` | Gerbers |
| `gerber/CCTRL_rev0.2_PickAndPlace_*.xlsx` | Component placement for assembly |
| `gerber/CCTRL_rev0.2_BOM_*.xlsx` | Board BOM as exported from EasyEDA, with LCSC part numbers |
| `bom/CCTRL_rev0.2_BOM_hand-assembly.csv` | Through-hole parts, DevKit socket headers and off-board parts, for hand assembly |

Upload the gerbers, BOM and pick-and-place to JLCPCB. Parts you choose to fit yourself, the DevKit socket headers and the off-board parts are listed with order numbers in the hand-assembly BOM. Steps are in the [Build Guide](../docs/BUILD.md#2-order-the-pcb).

## Ratings

Up to 2.4 A per panel and 6 A total. Fuse caps: 3 A per panel, 7.5 A at the input. See "Ratings and Fuse Selection" in the top-level README.

## Display

MSP3526 3.5" ST7796U IPS module with FT6336U capacitive touch. Datasheets, dimension drawings and a 3D model are on [LCDwiki](https://www.lcdwiki.com/3.5inch_IPS_SPI_Module_ST7796).

## License

The hardware design is released under the [CERN Open Hardware Licence v2, Strongly Reciprocal](LICENSE) (CERN-OHL-S-2.0).

Source Location: https://github.com/lautus-creations/key-light-controller

If you distribute boards or products based on this design, the license requires you to make the complete design source, including your changes, available under the same license, and to keep this notice and the Source Location with the product.

