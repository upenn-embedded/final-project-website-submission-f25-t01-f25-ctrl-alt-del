#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdbool.h>
#include "uart.h"

volatile uint32_t millis = 0;

ISR(TIMER0_COMPA_vect)
{
    millis++;
}

void timer0_init(void)
{
    TCCR0A = (1 << WGM01);
    TCCR0B = (1 << CS01) | (1 << CS00);   // prescale 64
    OCR0A = 249;                          // 1ms
    TIMSK0 = (1 << OCIE0A);
    sei();
}

// ---------- ADC ----------
void adc_init(void)
{
    ADMUX = (1 << REFS0);   // AVcc
    ADCSRA = (1 << ADEN) |
             (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

uint16_t adc_read(uint8_t ch)
{
    ADMUX = (ADMUX & 0xF8) | (ch & 7);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADC;
}

// ---------- Flex ?? ----------
#define ADC_TH_HIGH 500
#define ADC_TH_LOW  200
#define DEBOUNCE_N  3

typedef enum { FLEX_LOW = 0, FLEX_HIGH = 1 } flex_level_t;

flex_level_t flex_from_adc(uint16_t v, flex_level_t last)
{
    if (v > ADC_TH_HIGH) return FLEX_HIGH;
    if (v < ADC_TH_LOW)  return FLEX_LOW;
    return last;
}

int main(void)
{
    uart_init();
    adc_init();
    timer0_init();

    printf("Windowed Gesture System Start.\r\n");

    flex_level_t f1 = FLEX_HIGH, f2 = FLEX_HIGH;
    flex_level_t sf1 = FLEX_HIGH, sf2 = FLEX_HIGH;
    uint8_t c1 = 0, c2 = 0;

    uint8_t pattern = 3;
    uint8_t last_pattern = 3;

    // ???????
    bool in_low = false;
    uint32_t low_enter_time = 0;
    uint32_t last_close_time = 0;
    bool close_lock = false;
    bool open_lock  = false;

    // 2 ?????
    uint32_t window_start = 0;
    bool win_close = false;
    bool win_open  = false;
    bool win_start = false;

    while (1)
    {
        // ===== 1. ?? & ?? =====
        uint16_t a0 = adc_read(0);
        uint16_t a1 = adc_read(1);

        f1 = flex_from_adc(a0, f1);
        f2 = flex_from_adc(a1, f2);

        if (f1 != sf1) {
            if (++c1 >= DEBOUNCE_N) { sf1 = f1; c1 = 0; }
        } else c1 = 0;

        if (f2 != sf2) {
            if (++c2 >= DEBOUNCE_N) { sf2 = f2; c2 = 0; }
        } else c2 = 0;

        last_pattern = pattern;
        pattern = ((sf1 & 1) << 1) | (sf2 & 1);  // 3,2,1,0

        // ===== 2. ??????????? =====

        // START_PAIR?10 ????????
        if (pattern == 2 && last_pattern != 2) {
            win_start = true;
        }

        // CLOSE?00 ?? >=150ms ???
        if (pattern == 0) {
            if (!in_low) {
                in_low = true;
                low_enter_time = millis;
                close_lock = false;
            }
            uint32_t dt = millis - low_enter_time;
            if (dt >= 150 && !close_lock) {
                win_close = true;       // ????????? close
                close_lock = true;
                last_close_time = millis;
            }
        } else {
            in_low = false;
            close_lock = false;
        }

        // OPEN?????? close ???11 ??? <300ms ???
        if (pattern == 3) {
            uint32_t dtc = millis - last_close_time;
            if (!open_lock) {
                if (dtc < 300) {
                    win_open = true;    // ????????? open
                }
                open_lock = true;
            }
        } else {
            open_lock = false;
        }

        // ===== 3. ? 2 ????? =====
        if (millis - window_start >= 2000) {
            if (win_open) {
                printf("OPEN\r\n");
            } else if (win_close) {
                printf("CLOSE\r\n");
            } else if (win_start) {
                printf("START_PAIR\r\n");
            }
            // ????
            window_start = millis;
            win_open = win_close = win_start = false;
        }

        _delay_ms(10);
    }
}
