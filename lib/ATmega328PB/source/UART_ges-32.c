/*
 * File:   ges-32.c
 * Author: wusirui
 *
 * Created on November 21, 2025, 11:07 AM
 */


#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

#include "uart0.h"
#include "lsm6dso.h"

typedef enum {
    GEST_NONE = 0,
    GEST_UP,
    GEST_DOWN,
    GEST_LEFT,
    GEST_RIGHT
} gesture_t;

int main(void)
{
    uart0_init();
    uart0_send_str("LSM6DSO gesture demo start\r\n");

    LSM6DSO_Init();

    int16_t ax, ay, az;
    int16_t gx, gy, gz;

    gesture_t last_gesture = GEST_NONE;

    // More realistic threshold: ~0.4?0.5 g
    // 1 g ? 16384 LSB
    const int16_t THRESH = 7000;

    while (1)
    {
        LSM6DSO_ReadAccelRaw(&ax, &ay, &az);
        LSM6DSO_ReadGyroRaw(&gx, &gy, &gz);

        gesture_t g = GEST_NONE;
        
        if (ay > THRESH)          g = GEST_UP;
        else if (ay < -THRESH)    g = GEST_DOWN;
        else if (ax > THRESH)     g = GEST_LEFT;
        else if (ax < -THRESH)    g = GEST_RIGHT;

        // Debug print accelerometer (AX=xxxx!)
        uart0_send_str("AX=");
        uart0_send_int(ax);
        uart0_send_str(" AY=");
        uart0_send_int(ay);
        uart0_send_str(" AZ=");
        uart0_send_int(az);
        uart0_send_str("\r\n");

        // Send only when gesture changes and not NONE
        if (g != last_gesture && g != GEST_NONE)
        {
            switch (g)
            {
                case GEST_UP:    uart0_send_byte('U'); break;
                case GEST_DOWN:  uart0_send_byte('D'); break;
                case GEST_LEFT:  uart0_send_byte('L'); break;
                case GEST_RIGHT: uart0_send_byte('R'); break;
                default: break;
            }
            uart0_send_str("\r\n");   // nice for Serial Monitor
            last_gesture = g;
        }
        else if (g == GEST_NONE)
        {
            last_gesture = GEST_NONE;
        }

        _delay_ms(200);  // 5 Hz update; change to 50?100ms if you want faster
    }
}
