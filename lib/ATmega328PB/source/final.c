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

// 阈值 & 去抖：比较钝一点
#define ADC_TH_HIGH 650
#define ADC_TH_LOW  150
#define DEBOUNCE_N  6

// PAIR 的“准备时间 + 长按时间” & 与 CLOSE 的间隔（ms）
#define PAIR_READY_MS  400   // pattern != 2 稳定 400ms 后才允许下一次配对
#define PAIR_HOLD_MS   600   // pattern == 2 连续 600ms 才算配对
#define NO_CLOSE_MS    600   // 最近 600ms 内不能有 CLOSE，避免刚 close 顺带触发

typedef enum { FLEX_LOW = 0, FLEX_HIGH = 1 } flex_level_t;

flex_level_t flex_from_adc(uint16_t v, flex_level_t last)
{
    if (v > ADC_TH_HIGH) return FLEX_HIGH;
    if (v < ADC_TH_LOW)  return FLEX_LOW;
    return last;
}

// ===================== IR PWM (Timer1 / OC1A = PB1) =====================
#define IR_TOP   420       // 38kHz TOP
#define IR_DUTY  140       // 33% duty

void IR_PWM_Init(void)
{
    DDRB |= (1 << DDB1);   // PB1 output (OC1A)

    // Timer1 Fast PWM mode 14 (TOP = ICR1)
    TCCR1A = 0;
    TCCR1B = 0;

    ICR1 = IR_TOP;
    TCCR1A |= (1 << COM1A1);                // Clear OC1A on match
    TCCR1A |= (1 << WGM11);
    TCCR1B |= (1 << WGM13) | (1 << WGM12);  // mode 14

    OCR1A = 0;                              // initially off
    TCCR1B |= (1 << CS10);                  // no prescale
}

void IR_On(void)  { OCR1A = IR_DUTY; }
void IR_Off(void) { OCR1A = 0; }

// pairing burst：短短几串 38kHz
void ir_start_pair(void)
{
    for (uint8_t i = 0; i < 10; i++) {
        IR_On();
        _delay_us(800);
        IR_Off();
        _delay_us(400);
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

    // Flex gesture logic
    bool in_low = false;
    uint32_t low_enter_time = 0;
    uint32_t last_close_time = 0;
    bool close_lock = false;
    bool open_lock = false;

    // PAIR 检测状态
    bool in_pair = false;            // 当前是否在 pattern==2 的“长按计时”中
    uint32_t pair_enter_time = 0;    // 进入 pattern==2 的时间
    bool pair_armed = false;         // 是否已经在 pattern!=2 稳定过一段时间，允许下一次配对
    uint32_t not2_start_time = 0;    // 进入 pattern!=2 的时间，用来算 PAIR_READY_MS

    // 2s window
    uint32_t window_start = 0;
    bool win_open = false;
    bool win_close = false;
    bool win_start = false;

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

        // ---------- 1) 在 pattern != 2 时进入“准备期” ----------
        if (pattern != 2) {
            // 一旦离开 2，就不再认为在长按
            in_pair = false;

            if (!pair_armed) {
                if (not2_start_time == 0) {
                    // 刚刚进入非 2 状态，开始计时
                    not2_start_time = millis;
                } else if (millis - not2_start_time >= PAIR_READY_MS) {
                    // 非 2 状态稳定了一段时间，可以“上膛”等待下一次路过 2
                    pair_armed = true;
                }
            }
        } else {
            // pattern == 2，停止非 2 的计时
            not2_start_time = 0;
        }

        // ---------- 2) 只有在 pair_armed 状态下，路过 2 + 持续 才触发 ----------
        if (pair_armed && pattern == 2) {
            if (!in_pair) {
                // 从非2/准备好状态第一次进入2，开始计时
                in_pair = true;
                pair_enter_time = millis;
            } else {
                // 已经在2里，检查长按时间和 close 间隔
                if (!win_start &&
                    (millis - pair_enter_time >= PAIR_HOLD_MS) &&
                    (millis - last_close_time >= NO_CLOSE_MS)) {

                    win_start = true;   // 交给 2s 窗口真正发 'P'
                    pair_armed = false; // 用掉一次配对机会，必须再回到非2稳定一段时间才能再配
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
                last_close_time = millis;   // 记录 CLOSE 时间，PAIR 会避开最近的 close
            }
        } else {
            in_low = false;
            close_lock = false;
        }

        // --- OPEN: pattern==3 within 300ms after close ---
        if (pattern == 3) {
            if (!open_lock) {
                if (millis - last_close_time < 300)
                    win_open = true;
                open_lock = true;
            }
        } else {
            open_lock = false;
        }

        // ===== 2s WINDOW OUTPUT =====
        if (millis - window_start >= 2000)
        {
            if (win_open) {
                uart0_send_str("C\r\n");
            }
            else if (win_close) {
                uart0_send_str("O\r\n");
            }
            else if (win_start) {
                uart0_send_str("P\r\n");
                ir_start_pair();
            }

            window_start = millis;
            win_open = win_close = win_start = false;

            // 这一轮结束后，确保长按状态清空
            in_pair = false;
        }

        // ===== IMU (every 200ms) =====
        if (millis - last_imu_time >= 200)
        {
            last_imu_time = millis;

            LSM6DSO_ReadAccelRaw(&ax, &ay, &az);
            LSM6DSO_ReadGyroRaw(&gx, &gy, &gz);

            gesture_t g = GEST_NONE;
            if      (ay >  TH) g = GEST_UP;
            else if (ay < -TH) g = GEST_DOWN;
            else if (ax >  TH) g = GEST_LEFT;
            else if (ax < -TH) g = GEST_RIGHT;

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