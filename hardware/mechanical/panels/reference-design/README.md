# Panel Reference Design

A working panel design for this controller, provided as a reference. Any 24 V warm/cool LED panel within the controller's ratings can be used.

The controller drives up to three panels, each with warm and cool LED channels.

> [!WARNING]
> **Safety and liability**
>
> Building and using this project involves soldering, a mains-powered 24 V supply, and currents of several amps. Mistakes can cause electric shock, fire, burns, or damage to equipment. Build it only if you are competent to do this kind of work, follow the safety instructions for your power supply and tools, and comply with local electrical codes. Never work on the controller or panels while they are powered.
>
> By building or using this project, you acknowledge these risks and voluntarily assume all risk arising from it, including the risk of personal injury or death and of damage to or loss of property. The design, firmware, and documentation are provided "as is" and "with all faults," without warranty of any kind, express or implied. The authors accept no liability for personal injury, death, or property damage or loss arising from their use. This notice is in addition to the warranty disclaimers and limitations of liability in this repository's licenses.

## This Design

![Panel mounted on a desk boom, unlit](../../../../docs/images/photos/panel-front.jpg)

![Through the diffuser: alternating warm and cool LEDs](../../../../docs/images/photos/panel-lit.jpg)

Three identical panels, each housing a single warm+cool LED channel pair:

- **Overall dimensions:** 255 × 128.5 mm
- **LED strips:** Yuji YJ-BC-HRB-2835-24-2765, 100 mm segments. 5 strips × 200 mm = 1.0 m of strip per panel, about 0.32 A per channel at 24 V at full output.
- **Diffuser:** 3 mm acrylic, 5 × 10 in (the same size as the backing plate), with Rosco Cinegel #3006 Tough Spun diffusion film on the inside face
- **LED-to-diffuser gap:** 30 mm
- **Interior:** Matte black ABS lining
- **Backing:** Aluminum plate, 5 × 10 × 0.125 in (127 × 254 × 3.2 mm) ([the plate used](https://www.amazon.com/dp/B0BFW3W1JN))
- **Light trap lip:** 6–10 mm
- **Ventilation:** Passive

## Parts

| File | Description |
|---|---|
| `Key Light - Case.stl` | Main panel body |
| `Key Light - Bezel.stl` | Front bezel / diffuser frame |
| `Key Light - Retainer Sleeve.stl` | Diffuser retainer |
| `Key Light Housing.step` | Full assembly STEP file (editable in any CAD tool) |
| `Key Light Housing.f3z` | Fusion 360 source archive |

## Hardware (per panel)

- 7 M3 × 5 × 4 mm heat-set inserts (Voron standard, the same as the controller enclosure)
- 7 M3 flat head screws
- 1 or 2 1/4-20 heat-set inserts for mounting: one is required, the second is optional. The build used [these](https://www.amazon.com/dp/B078458CZY): 12.7 mm (0.5 in) long, 9.52 mm knurl diameter, 8.0 mm pilot end.

For three panels: 21 M3 inserts, 21 M3 flat head screws and 3 to 6 1/4-20 inserts.

## Connector

Each panel connects to the controller via a GX12-3 aviation connector:
- **Pin 1:** +24V
- **Pin 2:** Warm LED −
- **Pin 3:** Cool LED −
- **Shell:** Isolated (not grounded)

Use the same pinout on every panel and connector.

## Build Your Own

The controller is not tied to this geometry, strip density or diffuser. Any 24 V panel with separate warm and cool channels works, within the ratings in the top-level README (2.4 A per panel). Run Diagnostics > Check and save a reference after connecting a new panel.
