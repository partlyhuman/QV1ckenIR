#pragma once

// Which UART to use IRDA on
#ifndef IRDA_UART_NUM
#define IRDA_UART_NUM 1
#endif

// Builtin LED
#ifndef PIN_LED
#define PIN_LED LED_BUILTIN
#endif

#define LED_ON HIGH
#define LED_OFF LOW

// Builtin button
#ifndef PIN_BUTTON
#define PIN_BUTTON 0
#endif

// Vishay TFDU4101
#ifndef PIN_IRDA_TX
#define PIN_IRDA_TX 10
#endif
#ifndef PIN_IRDA_RX
#define PIN_IRDA_RX 13
#endif