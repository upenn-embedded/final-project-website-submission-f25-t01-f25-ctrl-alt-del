/*
 * Combined IMU + Flex + IR pairing system
 * MCU: ATmega328PB
 * UART: uart0
 * IR LED: PB1(OC1A), Timer1 Fast PWM 38kHz
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdbool.h>
#include <stdint.h>

#include "uart0.h"
#include "lsm6dso.h"

// ===================== Timer0: 1ms millis =====================
volatile uint32_t millis = 0;

ISR(TIMER0_COMPA_vect)
{
    millis++;
}

void timer0_init(void)
{
    TCCR0A = (1 << WGM01);                  // CTC
    TCCR0B = (1 << CS01) | (1 << CS00);     // prescale 64
    OCR0A = 249;                            // 1ms
    TIMSK0 = (1 << OCIE0A);                 // interrupt enable
    sei();
}

// ===================== ADC for Flex Sensors =====================
void adc_init(void)
{
    ADMUX = (1 << REFS0);   // AVcc reference
    ADCSRA = (1 << ADEN) |
             (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);  // prescale 128
}

uint16_t adc_read(uint8_t ch)
{
    ADMUX = (ADMUX & 0xF8) | (ch & 0x07);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADC;
}

// ===================== Flex logic =====================

// ?? & ????????
#define ADC_TH_HIGH 600
#define ADC_TH_LOW  200
#define DEBOUNCE_N  4

// PAIR ???ms?
#define PAIR_READY_MS   400   // pattern != 2 ?? 400ms ?????????
#define PAIR_HOLD_MS    600   // pattern == 2 ?? 600ms ????
#define NO_CLOSE_MS     600   // ?? 600ms ???? CLOSE
#define PAIR_MIN_GAP_MS 1500  // ?????????? 1.5s

// OPEN ???ms?
#define OPEN_STABLE_MS        250   // pattern == 3 ????
#define CLOSE2OPEN_MAX_MS     500   // CLOSE ?? 500ms ?? OPEN ??
#define CLOSE2OPEN_MIN_DELAY  100   // CLOSE ???? 100ms ?? OPEN??????????

typedef enum { FLEX_LOW = 0, FLEX_HIGH = 1 } flex_level_t;

flex_level_t flex_from_adc(uint16_t v, flex_level_t last)
{
    if (v > ADC_TH_HIGH) return FLEX_HIGH;
    if (v < ADC_TH_LOW)  return FLEX_LOW;
    return last;
}

// ===================== IR PWM (Timer1 / OC1A = PB1) =====================
#define IR_TOP   420       // 38kHz TOP
#define IR_DUTY  140       // ~33% duty

void IR_PWM_Init(void)
{
    DDRB |= (1 << DDB1);   // PB1 output (OC1A)

    // Timer1 Fast PWM, mode 14, TOP = ICR1
    TCCR1A = (1 << WGM11);
    TCCR1B = (1 << WGM13) | (1 << WGM12);

    ICR1  = IR_TOP;
    OCR1A = 0;

    // ????? OC1A?? PORTB ????????
    TCCR1A &= ~((1 << COM1A1) | (1 << COM1A0));
    PORTB  &= ~(1 << PB1);

    // ???
    TCCR1B |= (1 << CS10);
}

// ??????? OC1A + ???? PB1
void IR_Off(void)
{
    TCCR1A &= ~((1 << COM1A1) | (1 << COM1A0)); // disconnect OC1A
    OCR1A = 0;
    PORTB &= ~(1 << PB1);                       // LOW
}

// ????? OC1A??? 38kHz PWM
void IR_On(void)
{
    OCR1A = IR_DUTY;
    TCCR1A &= ~(1 << COM1A0);
    TCCR1A |=  (1 << COM1A1); // Clear OC1A on compare match
}

// ??????????? pairing burst?600us on + 600us off × 40
// ?? 48ms?TSOP / Adafruit 5939 ????
void IR_SendPairBurst(void)
{
    for (uint8_t i = 0; i < 40; i++) {
        IR_On();
        _delay_us(600);
        IR_Off();
        _delay_us(600);
    }
}

// ===================== IMU Gesture =====================
typedef enum {
    GEST_NONE,
    GEST_UP,
    GEST_DOWN,
    GEST_LEFT,
    GEST_RIGHT
} gesture_t;

// ===================== main =====================
int main(void)
{
    uart0_init();
    adc_init();
    timer0_init();
    IR_PWM_Init();
    LSM6DSO_Init();
    uart0_send_str("System Start\r\n");

    // Flex states
    flex_level_t f1 = FLEX_HIGH, f2 = FLEX_HIGH;
    flex_level_t sf1 = FLEX_HIGH, sf2 = FLEX_HIGH;
    uint8_t c1 = 0, c2 = 0;
    uint8_t pattern = 3, last_pattern = 3;

    // CLOSE ??
    bool in_low = false;
    uint32_t low_enter_time = 0;
    uint32_t last_close_time = 0;
    bool close_lock = false;

    // OPEN ??
    bool in_open = false;
    uint32_t open_enter_time = 0;
    bool open_lock = false;

    // PAIR ????
    bool in_pair = false;            // ????? pattern==2 ????????
    uint32_t pair_enter_time = 0;    // ?? pattern==2 ???
    bool pair_armed = false;         // pattern!=2 ???????
    uint32_t not2_start_time = 0;    // ?? pattern!=2 ???
    uint32_t last_pair_time = 0;     // ????????

    // 2s window?????? C / O?
    uint32_t window_start = 0;
    bool win_open = false;
    bool win_close = false;

    // IMU
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    gesture_t last_gesture = GEST_NONE;
    const int16_t TH = 7000;
    uint32_t last_imu_time = 0;

    while (1)
    {
        // ===== FLEX =====
        uint16_t a0 = adc_read(0);
        uint16_t a1 = adc_read(1);

        // --- ????? ADC ? ---
        /*
        uart0_send_str("ADC0=");
        uart0_send_int(a0);
        uart0_send_str("  ADC1=");
        uart0_send_int(a1);
        uart0_send_str("\r\n");
         */


        f1 = flex_from_adc(a0, f1);
        f2 = flex_from_adc(a1, f2);

        if (f1 != sf1) {
            if (++c1 >= DEBOUNCE_N) {
                sf1 = f1;
                c1 = 0;
            }
        } else {
            c1 = 0;
        }

        if (f2 != sf2) {
            if (++c2 >= DEBOUNCE_N) {
                sf2 = f2;
                c2 = 0;
            }
        } else {
            c2 = 0;
        }

        last_pattern = pattern;
        pattern = ((sf1 & 1) << 1) | (sf2 & 1);   // bit1: flex1, bit0: flex2

        // ---------- PAIR 1) pattern != 2 ???????? ----------
        if (pattern != 2) {
            in_pair = false;

            if (!pair_armed) {
                if (not2_start_time == 0) {
                    not2_start_time = millis;
                } else if (millis - not2_start_time >= PAIR_READY_MS) {
                    pair_armed = true;   // ?2?????????
                }
            }
        } else {
            not2_start_time = 0;
        }

        // ---------- PAIR 2) ?? 2 + ????? P + IR burst ----------
        if (pair_armed && pattern == 2) {
            if (!in_pair) {
                in_pair = true;
                pair_enter_time = millis;
            } else {
                if ((millis - pair_enter_time >= PAIR_HOLD_MS) &&
                    (millis - last_close_time >= NO_CLOSE_MS)   &&
                    (millis - last_pair_time  >= PAIR_MIN_GAP_MS))
                {
                    // ???? P
                    uart0_send_str("P\r\n");

                    // ?????? 38kHz ?? burst?? 48ms?
                    IR_SendPairBurst();

                    last_pair_time = millis;
                    pair_armed     = false;
                    in_pair        = false;
                }
            }
        }

        // --- CLOSE (pattern==0, >=150ms) ---
        if (pattern == 0) {
            if (!in_low) {
                in_low = true;
                low_enter_time = millis;
                close_lock = false;
            }
            if (!close_lock && (millis - low_enter_time >= 150)) {
                win_close = true;
                close_lock = true;
                last_close_time = millis;
            }
        } else {
            in_low = false;
            close_lock = false;
        }

        // --- OPEN: pattern==3 ????????? close ????? ---
        if (pattern == 3) {
            if (!in_open) {
                in_open = true;
                open_enter_time = millis;
            }

            if (!open_lock &&
                (millis - open_enter_time >= OPEN_STABLE_MS) &&
                (millis - last_close_time >= CLOSE2OPEN_MIN_DELAY) &&
                (millis - last_close_time <= CLOSE2OPEN_MAX_MS))
            {
                win_open = true;
                open_lock = true;
            }
        } else {
            in_open = false;
            open_lock = false;
        }

        // ===== 2s WINDOW OUTPUT???? C / O? =====
        if (millis - window_start >= 2000)
        {
            if (win_open) {
                uart0_send_str("O\r\n");
            }
            else if (win_close) {
                uart0_send_str("C\r\n");
            }

            window_start = millis;
            win_open = win_close = false;
            in_pair = false;
        }

        // ===== IMU (every 200ms) =====
        if (millis - last_imu_time >= 200)
        {
            last_imu_time = millis;

            LSM6DSO_ReadAccelRaw(&ax, &ay, &az);
            LSM6DSO_ReadGyroRaw(&gx, &gy, &gz);

            gesture_t g = GEST_NONE;
            if      (ax >  TH) g = GEST_UP;
            else if (ax < -TH) g = GEST_DOWN;
            else if (ay >  TH) g = GEST_RIGHT;
            else if (ay < -TH) g = GEST_LEFT;

            if (g != last_gesture && g != GEST_NONE) {
                switch (g) {
                    case GEST_UP:    uart0_send_byte('U'); break;
                    case GEST_DOWN:  uart0_send_byte('D'); break;
                    case GEST_LEFT:  uart0_send_byte('L'); break;
                    case GEST_RIGHT: uart0_send_byte('R'); break;
                    default: break;
                }
                uart0_send_str("\r\n");
                last_gesture = g;
            } else if (g == GEST_NONE) {
                last_gesture = GEST_NONE;
            }
        }

        _delay_ms(10);
    }
}




