# DevKit revision test

A throwaway PlatformIO sketch to tell whether the ESP32-S3-DevKitC-1 is **v1.1** or **v1.0**, because the board's silkscreen doesn't always say and the revision determines whether **GPIO 48** is free.

## Why it matters

The onboard RGB LED moved pins between revisions:

| Revision | RGB LED pin | Consequence for this project |
|---|---|---|
| **v1.1** | GPIO 38 | GPIO 48 is a free GPIO — OK for PC-cool PWM (`GPIO_PWM_PC_COOL`) |
| **v1.0** | GPIO 48 | GPIO 48 drives the onboard LED — **conflicts** with PC-cool PWM |

## Run it

1. Open this folder (`tools/devkit-rev-test`) in VS Code with the PlatformIO extension.
2. Connect the **bare DevKit** by USB (see note below).
3. Build + Upload (PlatformIO: Upload), then open the Serial Monitor (115200).

## Read the result

Watch the single onboard RGB LED next to the BOOT/RST buttons:

- **Blinks RED only** → GPIO 38 controls it → board is **v1.1** → GPIO 48 is free, no change needed.
- **Blinks BLUE only** → GPIO 48 controls it → board is **v1.0** → GPIO 48 is tied to the LED; move the PC-cool PWM to a spare GPIO.
- **Never lights** → neither pin is the RGB — check the board, or the Arduino-ESP32 core version (see below).

## Notes

- **Run on the bare DevKit over USB.** If the DevKit is seated in the PCB, the GPIO-48 phase briefly pulses the PC-cool MOSFET gate and you'll see that panel flicker — harmless, just cleaner to test standalone.
- `neopixelWrite()` is the Arduino-ESP32 core 2.0.x API (matches the pinned platform `espressif32@^6.9.0`). On a 3.x core the equivalent is `rgbLedWrite(pin, r, g, b)` — same arguments.
- Delete this folder once you've confirmed the revision; it's not part of the firmware.
