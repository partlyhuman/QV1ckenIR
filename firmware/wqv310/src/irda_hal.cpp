#include "irda_hal.h"

#include <hal/uart_hal.h>

#include "config.h"

// Communicate with the TFDU4101. Connecting UART to the RX/TX pins as normal is not sufficient.
// It needs to use the IRDA physical layer https://www.vishay.com/docs/82513/physicallayer.pdf
// which is a fairly simple transformation of typical UART signalling.
//
// ESP32 supports this in hardware.
// Setting Serial.setMode(UART_MODE_IRDA) is half the battle, we also need this special sauce:
// UART.conf0.irda_tx_en = true while transmitting.

HardwareSerial IRDA = HardwareSerial(IRDA_UART_NUM);
static uart_dev_t *IRDA_UART = UART_LL_GET_HW(IRDA_UART_NUM);

void IRDA_setup(HardwareSerial &serial) {
    serial.begin(115200, SERIAL_8N1, PIN_IRDA_RX, PIN_IRDA_TX);
    serial.setMode(UART_MODE_IRDA);
}

void IRDA_tx(bool b) {
    IRDA_UART->conf0.irda_tx_en = b;
}