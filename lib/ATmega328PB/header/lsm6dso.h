#ifndef LSM6DSO_H
#define LSM6DSO_H

#include <stdint.h>

// 7-bit I2C address of LSM6DSO (SparkFun Qwiic default)
#define LSM6DSO_I2C_ADDR  0x6B

// --- Register map (only what we need) ---
#define LSM6DSO_REG_WHO_AM_I   0x0F
#define LSM6DSO_REG_CTRL1_XL   0x10
#define LSM6DSO_REG_CTRL2_G    0x11
#define LSM6DSO_REG_CTRL3_C    0x12

#define LSM6DSO_REG_OUTX_L_G   0x22
#define LSM6DSO_REG_OUTX_H_G   0x23
#define LSM6DSO_REG_OUTY_L_G   0x24
#define LSM6DSO_REG_OUTY_H_G   0x25
#define LSM6DSO_REG_OUTZ_L_G   0x26
#define LSM6DSO_REG_OUTZ_H_G   0x27

#define LSM6DSO_REG_OUTX_L_XL  0x28
#define LSM6DSO_REG_OUTX_H_XL  0x29
#define LSM6DSO_REG_OUTY_L_XL  0x2A
#define LSM6DSO_REG_OUTY_H_XL  0x2B
#define LSM6DSO_REG_OUTZ_L_XL  0x2C
#define LSM6DSO_REG_OUTZ_H_XL  0x2D

// --- Public API ---

// Initialize AVR TWI0 (I2C on PC4/PC5) to 100 kHz
void LSM6DSO_TWI_Init(void);

// Configure LSM6DSO (I2C, 104 Hz, ±2g, ±250 dps)
void LSM6DSO_Init(void);

// Read WHO_AM_I (should return 0x6C)
uint8_t LSM6DSO_WhoAmI(void);

// Read raw accelerometer data (LSB)
void LSM6DSO_ReadAccelRaw(int16_t *ax, int16_t *ay, int16_t *az);

// Read raw gyroscope data (LSB)
void LSM6DSO_ReadGyroRaw(int16_t *gx, int16_t *gy, int16_t *gz);

// Convert raw accel to g (for ±2g FS)
float LSM6DSO_AccelLSB_to_g(int16_t raw);

// Convert raw gyro to dps (for ±250 dps FS)
float LSM6DSO_GyroLSB_to_dps(int16_t raw);

#endif // LSM6DSO_H