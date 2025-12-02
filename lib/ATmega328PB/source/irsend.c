/*
 * File:   irsend.c
 * Author: wusirui
 *
 * Created on December 1, 2025, 10:15 AM
 */


#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdbool.h>

/*
 * ??:
 *  - PB1 / OC1A ?? 38kHz ???????? -> IR LED
 *  - ?? Timer1 Fast PWM (Mode 14, TOP = ICR1)
 */

// 38kHz ??? TOP
#define IR_TOP     420
// ?? 1/3 ???
#define IR_DUTY    140

void IR_PWM_Init(void)
{
    // PB1 (OC1A) ????
    DDRB |= (1 << DDB1);

    // Timer1: Fast PWM, TOP = ICR1, ?????? OC1A
    // WGM13:0 = 14 (1110b)
    TCCR1A = 0;
    TCCR1B = 0;

    // ?? TOP
    ICR1 = IR_TOP;

    // ?????: Clear OC1A on compare match, set at BOTTOM
    TCCR1A |= (1 << COM1A1);

    // Fast PWM, mode 14: WGM13=1,WGM12=1,WGM11=1,WGM10=0
    TCCR1A |= (1 << WGM11);
    TCCR1B |= (1 << WGM13) | (1 << WGM12);

    // ??????? 0????
    OCR1A = 0;

    // ??? = 1
    TCCR1B |= (1 << CS10);
}

// ?? 38kHz?? OCR1A ????????
void IR_On(void)
{
    OCR1A = IR_DUTY;
}

// ?? 38kHz??????? 0????????
void IR_Off(void)
{
    OCR1A = 0;
}

// ???? 10ms ??? 600us ? burst
int main(void)
{
    IR_PWM_Init();

    while (1)
    {
        // ????? burst ????600us?
        IR_On();
        _delay_us(1000);
        IR_Off();

        // ?????????10ms?
        _delay_ms(250);
    }
}
