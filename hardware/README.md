# Hardware

Design files for the controller PCB (rev 0.2) and a reference panel design. Assembly steps are in the [Build Guide](../docs/BUILD.md).

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

If you distribute boards or products based on this design, the licence requires you to make the complete design source, including your changes, available under the same licence, and to keep this notice and the Source Location with the product.

