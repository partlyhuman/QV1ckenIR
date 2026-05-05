import FIRMWARE_DATA_URL from "data-url:./bin/release_2.0.bin";
import {ESPLoader, IEspLoaderTerminal, Transport} from "esptool-js";

const $progress = document.querySelector("progress");
const $button = document.querySelector("button");
const $err = document.querySelector("#error");
const $console = document.querySelector("#console");
const $step = document.querySelector("#step");

const ESP32_USB_ID = {
    usbProductId: 0x1001,
    usbVendorId: 0x303a,
};

function reset() {
    $err.classList.add("hidden");
    $err.innerHTML = "";
    $console.innerHTML = "";
    status('', 0);
    $progress.classList.add("hidden");
}

function showError(errorText: string) {
    status('', 0);
    $err.classList.remove("hidden");
    $err.innerHTML = errorText ?? "Error";
}

function log(str: string) {
    console.log(str);
    $console.innerHTML += "\n" + str;
}

function status(str: string | undefined, percent: number | undefined) {
    $progress.classList.remove("hidden");
    if (percent === undefined) {
        $progress.removeAttribute('value');
    } else {
        $progress.value = percent;
    }
    if (str !== undefined) {
        $step.innerHTML = str;
    }
}

async function go() {
    $button.disabled = true;
    try {
        reset();
        if (!("serial" in navigator)) {
            showError(`This browser doesn't support WebUSB. Please try this on a <a
            href="https://caniuse.com/webusb">compatible browser</a>.`)
            return;
        }

        let port;
        try {
            port = await navigator.serial.requestPort({filters: [ESP32_USB_ID]});
        } catch {
            showError(`The browser isn't paired with your USB device. Please give it permission to connect and choose the USB JTAG/Serial debug unit device.`)
            return;
        }

        // Create transport instance
        const transport = new Transport(port, true);

        // Optional: Create a terminal interface for logging
        const consoleTerm: IEspLoaderTerminal = {
            clean() {
                // console.clear();
            },
            writeLine(data: string) {
                console.trace(data);
            },
            write(data: string) {
                console.trace(data);
            },
        };

        // Configure loader options
        const esploader = new ESPLoader({
            transport: transport,
            baudrate: 115200,
            // terminal: consoleTerm,
            debugLogging: false,
        });

        try {
            status("Talking to device...", undefined);
            // Connect and detect chip (this will reset the device)
            const chipName = await esploader.main();
            $progress.value = 0;
            status(`Connected to ${chipName}!`, 0);
        } catch (error) {
            showError(`Couldn't connect. Check that USB is connected and the screen should be blank. See above for special instructions when updating.`)
            console.error("Failed to connect:", error);
            return;
        }

        const blob = await (await fetch(FIRMWARE_DATA_URL)).blob();
        const firmwareBytes = new Uint8Array(await blob.arrayBuffer());
        const firmwareAddress = 0; // Starting address in flash

        status(`Flashing new firmware...`, 0);
        try {

            await esploader.writeFlash({
                fileArray: [
                    {data: firmwareBytes, address: firmwareAddress}
                ],
                flashMode: "dio", // Flash mode: "qio", "qout", "dio", "dout"
                flashFreq: "40m", // Flash frequency: "80m", "40m", "26m", "20m", etc.
                flashSize: "4MB", // Flash size: "256KB", "512KB", "1MB", "2MB", "4MB", etc.
                eraseAll: true, // Set to true to erase entire flash before writing
                compress: true, // Compress data during transfer
                reportProgress: (fileIndex, written, total) => {
                    status(undefined, written / total);
                },
            });
        } catch (e) {
            showError(`Error flashing: ${e.message}`);
            return;
        }

        // await esploader.after("hard_reset");

        status(`Success! It is now safe to unplug the WQV Blink!`, 1);
    } finally {
        $button.disabled = false;
    }
}

document.querySelector('#go').addEventListener('click', go);