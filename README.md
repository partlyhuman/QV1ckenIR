This is an easy-to-use, self-contained device that retrieves photos from the extremely cool 2000-era [Casio WQV-1 camera watch](https://www.casio.com/us/watches/50th/Heritage/2000s/).

<img src="img/printed.jpg"/>

This project would not be possible without the reverse engineering work of [Marcus Gröber](https://www.mgroeber.de/).

## Coming soon!

Pre-assembled devices will be available for purchase soon!

## Usage

1. Plug in the device to your computer via USB
2. Following the instructions on the screen, enter PC sync mode on the watch
3. Aim the watch at the IR transciever
4. The screen will show download progress
5. Once completed, the device will show on your computer as a USB drive
6. Enjoy your photos!

## Build options

Currently this builds on ESP32-S2 and -S3 based boards. You can choose to include a common 0.96" monochrome SSD1306 screen or not based on the Platform.io environment.

Other ESP32 SoCs like the C3 lack the hardware to support USB Device mode, which we use to mount as a USB drive. See the [Espressif USB peripheral FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/usb.html).

PSRAM is used if found on your board, but is not required. Flash space large enough to store 99 images (<1MB) as well as the program is required, should not be an issue.

<img src="img/breakout.jpg"/>

A Vishay TFDU4101 is the only required external component, used for bidirectional IRDA communication with the watch. It is still in production and widely available for cheap! In this repository you will find a breakout PCB that will give it 1.54mm standard header pins. Some external capacitors and resistors are recommended but actually none are required, see the datasheet. The repository also includes PCB(s) that integrate all components and passives with popular ESP32S2/3 dev board(s).
