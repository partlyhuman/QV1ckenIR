# QVIckenIR
(is that a cool name?) A Casio WQV-1 USB Sync Dongle

This is an easy-to-use, self-contained device that retrieves photos from the extremely cool 2000-era [Casio WQV-1 camera watch](https://www.casio.com/us/watches/50th/Heritage/2000s/).
This project would not be possible without the reverse engineering work of [Marcus Gröber](https://www.mgroeber.de/).

## Purchase

Pre-assembled devices ars available in limited quantities on my [Ko-Fi shop](https://ko-fi.com/s/76fb4d3271)!

## Compatibility

* WQV-1 ✅
* WQV-2 ✅
* WQV-3 🔜
* WQV-10 🔜

## Usage

1. Plug in the device to your computer via USB
2. Following the instructions on the screen, enter PC sync mode on the watch
3. Aim the watch at the IR transciever
4. The screen will show download progress
5. Once completed, the device will show on your computer as a USB drive
6. Enjoy your photos!

## PCB Build

<img src="img/dongle.jpg" width="400"/>

The PCB, case, and firmware in this repository go together as pictured here.

### Bill of Materials

| Reference | Value | Details |
|----|----|----|
| C1 | 4.7u | 0805 Capacitor (optional) |
| R1 | 0R | 0805 Resistor (optional) |
| R2 | 47R | 0805 Resistor (optional) |
| U1 | ESP32-S3 Super Micro | https://www.aliexpress.com/item/1005007523988592.html |
| U2 | SSD1306_OLED_128x64 | https://www.aliexpress.com/item/1005007883712377.html |
| U3 | TFDU4101-TR3 | https://www.digikey.com/short/nrdh27mb |

R1 is used to save power to the IR LED. The TFDU4101 has built-in LED resistors so this can be 0R for maximum TX power. R2,C1 are a low-pass filter for the TFDU4101 logic. C1 is recommended 0.1uF or greater, but one or both can be bridged/omitted. Recommended values above from the TFDU4101 datasheet.

### PCB

Gerbers are included in [pcb/WQV1-S3-SuperMini/production/](pcb/WQV1-S3-SuperMini/production/). Fabricate with basic options, nothing fancy. 1.6mm thickness, HASL is just fine.

### Programming and assembly

Builds are automated using PlatformIO. In the [firmware/irda-esp32](firmware/irda-esp32) directory, run `pio run -e esp32_s3_supermini` or use the PlatformIO VSCode extension.

## DIY Build options

<img src="img/printed.jpg" width=400/>

Breakouts are included to make a hand-wired device possible using breadboards, perfboards, jumpers, etc.

Currently this builds on ESP32-S2 and -S3 based boards. You can choose to include a common 0.96" monochrome SSD1306 screen or not based on the Platform.io environment.

Other ESP32 SoCs like the C3 lack the hardware to support USB Device mode, which we use to mount as a USB drive. See the [Espressif USB peripheral FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/usb.html).

PSRAM is used if found on your board, but is not required. Flash space large enough to store 99 images (<1MB) as well as the program is required, should not be an issue.

<img src="img/breakout.jpg"/>

A [Vishay TFDU4101](https://www.vishay.com/en/product/81288/) is the only required external component, used for bidirectional IRDA communication with the watch. It is still in production and widely available for cheap! In this repository you will find breakout PCBs that will give it 2.54mm standard header pins, [one barebones](pcb/TFDU4101-breakout/), and a more [user-friendly one](pcb/TFDU4101-breakout-simplified/) which includes the passive components, and a solder bridge to optionally power the IR LED & logic from a single supply (recommended, up to +6V is acceptable according to the datasheet). The silkscreen gives two possible orientations of the TFDU4101 so you can choose to aim it upwards or outwards. Double-check pin 1 to make sure your orientation is correct.
