#define F_CPU 16000000UL   // internal 16 MHz

#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdint.h>

#include "uart.h"
#include "lsm6dso.h"

// Simple gesture enum
typedef enum {
    GEST_NONE = 0,
    GEST_UP,
    GEST_DOWN,
    GEST_LEFT,
    GEST_RIGHT
} gesture_t;

int main(void)
{
    uart_init();                    // your UART driver with printf support
    printf("LSM6DSO I2C + gesture demo\r\n");

    // Initialize IMU (I2C on PC4/PC5)
    LSM6DSO_Init();

    uint8_t who = LSM6DSO_WhoAmI();
    printf("WHO_AM_I = 0x%02X (expect 0x6C)\r\n", who);

    int16_t ax, ay, az;
    int16_t gx, gy, gz;

    gesture_t last_gesture = GEST_NONE;

    // Threshold in raw LSB; 1 g ≈ 16384 LSB, so 6000 ≈ 0.36 g
    const int16_t THRESH = 16000;

    while (1)
    {
        // Read raw accel + gyro
        LSM6DSO_ReadAccelRaw(&ax, &ay, &az);
        LSM6DSO_ReadGyroRaw(&gx, &gy, &gz);

        // ---- Gesture detection (very simple) ----
        gesture_t g = GEST_NONE;

        // Up / Down based on Y axis tilt
        if (ay > THRESH)
            g = GEST_UP;
        else if (ay < -THRESH)
            g = GEST_DOWN;
        // Left / Right based on X axis tilt
        else if (ax > THRESH)
            g = GEST_LEFT;
        else if (ax < -THRESH)
            g = GEST_RIGHT;
        else
            g = GEST_NONE;

        // Print raw values for debugging (optional)
        printf("ACC: %6d %6d %6d  |  GYRO: %6d %6d %6d\r\n",
               ax, ay, az, gx, gy, gz);

        // Only print gesture when it changes and is not NONE
        if (g != last_gesture && g != GEST_NONE)
        {
            switch (g)
            {
                case GEST_UP:
                    printf("Gesture: UP \r\n");
                    break;
                case GEST_DOWN:
                    printf("Gesture: DOWN \r\n");
                    break;
                case GEST_LEFT:
                    printf("Gesture: LEFT \r\n");
                    break;
                case GEST_RIGHT:
                    printf("Gesture: RIGHT \r\n");
                    break;
                default:
                    break;
            }
        }

        last_gesture = g;

        _delay_ms(50);   // ~20 Hz gesture update
    }

    return 0;
}