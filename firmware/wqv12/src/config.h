#pragma once

// Which UART to use IRDA on
#ifndef IRDA_UART_NUM
#define IRDA_UART_NUM 1
#endif

// Builtin LED
#ifndef PIN_LED
#define PIN_LED LED_BUILTIN
#endif

#define LED_ON LOW
#define LED_OFF HIGH

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

// I2C Display
#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA SDA
#endif
#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL SCL
#endif
#ifndef I2C_CLOCK
#define I2C_CLOCK 1000000UL
#endif

// RGB LED
#ifndef PIN_WS2812
#define PIN_WS2812 48
#endif