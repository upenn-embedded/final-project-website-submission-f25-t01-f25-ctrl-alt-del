#include <avr/io.h>
#include "uart0.h"

#ifndef F_CPU
#define F_CPU 16000000UL           // Your MCU clock: 16 MHz
#endif

#define UART0_BAUD     9600UL
#define UART0_UBRR_VAL ((F_CPU / (16UL * UART0_BAUD)) - 1)   // = 103

/**
 * Initialize UART0: 9600 baud, 8 data bits, 1 stop bit, no parity.
 */
void uart0_init(void)
{
    // Baud rate
    UBRR0H = (uint8_t)(UART0_UBRR_VAL >> 8);
    UBRR0L = (uint8_t)(UART0_UBRR_VAL & 0xFF);

    // Enable receiver + transmitter
    UCSR0B = (1 << RXEN0) | (1 << TXEN0);

    // Frame: 8N1
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

/**
 * Send one byte (blocking).
 */
void uart0_send_byte(uint8_t data)
{
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = data;
}

/**
 * Send a null-terminated string.
 */
void uart0_send_str(const char *s)
{
    while (*s) {
        uart0_send_byte((uint8_t)*s);
        s++;
    }
}

/**
 * Return non-zero if a byte is waiting in UDR0.
 */
uint8_t uart0_rx_available(void)
{
    return (UCSR0A & (1 << RXC0));
}

/**
 * Read one received byte (blocking).
 */
uint8_t uart0_recv_byte(void)
{
    while (!(UCSR0A & (1 << RXC0)));
    return UDR0;
}

/*
 * Send signed integer as ASCII text.
 */
void uart0_send_int(int16_t x)
{
    char buf[8];
    uint8_t i = 0;

    // Handle negative numbers
    if (x < 0) {
        uart0_send_byte('-');
        x = -x;
    }

    // Special case for zero
    if (x == 0) {
        uart0_send_byte('0');
        return;
    }

    // Convert digits (stored in reverse order)
    while (x > 0 && i < sizeof(buf)) {
        buf[i++] = (x % 10) + '0';
        x /= 10;
    }

    // Send digits in correct order
    while (i > 0) {
        uart0_send_byte(buf[--i]);
    }
}

