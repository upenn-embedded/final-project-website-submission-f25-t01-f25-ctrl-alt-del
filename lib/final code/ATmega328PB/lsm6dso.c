#include "lsm6dso.h"

#ifndef F_CPU
#define F_CPU 16000000UL   // internal 16 MHz
#endif

#include <avr/io.h>
#include <util/delay.h>

// ======================================================
//          TWI0 (I2C) low-level driver on PC4/PC5
// ======================================================

// ======================================================
//      TWI0 (I2C) for ATmega328PB  ?  PC4=SDA, PC5=SCL
// ======================================================

#define I2C_FREQ 100000UL   // 100 kHz

static void TWI0_Start(void);
static void TWI0_Stop(void);
static void TWI0_Write(uint8_t data);
static uint8_t TWI0_ReadAck(void);
static uint8_t TWI0_ReadNack(void);

void LSM6DSO_TWI_Init(void)
{
    // Prescaler = 1
    TWSR0 = 0x00;

    // TWBR0 formula:
    // SCL = F_CPU / (16 + 2*TWBR0*prescaler)
    TWBR0 = (uint8_t)(((F_CPU / I2C_FREQ) - 16) / 2);
}

// ---------------- Low-level I2C functions ----------------

static void TWI0_Start(void)
{
    TWCR0 = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR0 & (1 << TWINT)));
}

static void TWI0_Stop(void)
{
    TWCR0 = (1 << TWINT) | (1 << TWEN) | (1 << TWSTO);
}

static void TWI0_Write(uint8_t data)
{
    TWDR0 = data;
    TWCR0 = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR0 & (1 << TWINT)));
}

static uint8_t TWI0_ReadAck(void)
{
    TWCR0 = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
    while (!(TWCR0 & (1 << TWINT)));
    return TWDR0;
}

static uint8_t TWI0_ReadNack(void)
{
    TWCR0 = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR0 & (1 << TWINT)));
    return TWDR0;
}


// ======================================================
//          LSM6DSO register read/write helpers
// ======================================================

// Write a single register
static void LSM6DSO_WriteReg(uint8_t reg, uint8_t val)
{
    TWI0_Start();
    TWI0_Write((LSM6DSO_I2C_ADDR << 1) | 0);   // write
    TWI0_Write(reg);
    TWI0_Write(val);
    TWI0_Stop();
}

// Read multiple sequential registers
static void LSM6DSO_ReadRegs(uint8_t startReg, uint8_t *buf, uint8_t len)
{
    // Write register address
    TWI0_Start();
    TWI0_Write((LSM6DSO_I2C_ADDR << 1) | 0);   // write
    TWI0_Write(startReg);

    // Repeated START, then switch to read
    TWI0_Start();
    TWI0_Write((LSM6DSO_I2C_ADDR << 1) | 1);   // read

    for (uint8_t i = 0; i < len; i++)
    {
        if (i == (len - 1))
            buf[i] = TWI0_ReadNack();
        else
            buf[i] = TWI0_ReadAck();
    }

    TWI0_Stop();
}

// ======================================================
//                  Public API
// ======================================================

uint8_t LSM6DSO_WhoAmI(void)
{
    uint8_t id;
    LSM6DSO_ReadRegs(LSM6DSO_REG_WHO_AM_I, &id, 1);
    return id;
}

void LSM6DSO_Init(void)
{
    // Init I2C first
    LSM6DSO_TWI_Init();
    _delay_ms(20);

    // CTRL3_C: BDU=1 (bit6), IF_INC=1 (bit2) => 0x44
    LSM6DSO_WriteReg(LSM6DSO_REG_CTRL3_C, 0x44);

    // CTRL1_XL: accel 104 Hz, ±2g => 0x40
    LSM6DSO_WriteReg(LSM6DSO_REG_CTRL1_XL, 0x40);

    // CTRL2_G: gyro 104 Hz, ±250 dps => 0x40
    LSM6DSO_WriteReg(LSM6DSO_REG_CTRL2_G, 0x40);

    _delay_ms(20);
}

void LSM6DSO_ReadAccelRaw(int16_t *ax, int16_t *ay, int16_t *az)
{
    uint8_t buf[6];
    LSM6DSO_ReadRegs(LSM6DSO_REG_OUTX_L_XL, buf, 6);

    *ax = (int16_t)(buf[0] | ((uint16_t)buf[1] << 8));
    *ay = (int16_t)(buf[2] | ((uint16_t)buf[3] << 8));
    *az = (int16_t)(buf[4] | ((uint16_t)buf[5] << 8));
}

void LSM6DSO_ReadGyroRaw(int16_t *gx, int16_t *gy, int16_t *gz)
{
    uint8_t buf[6];
    LSM6DSO_ReadRegs(LSM6DSO_REG_OUTX_L_G, buf, 6);

    *gx = (int16_t)(buf[0] | ((uint16_t)buf[1] << 8));
    *gy = (int16_t)(buf[2] | ((uint16_t)buf[3] << 8));
    *gz = (int16_t)(buf[4] | ((uint16_t)buf[5] << 8));
}

// Convert accel raw LSB -> g, for ±2g FS
float LSM6DSO_AccelLSB_to_g(int16_t raw)
{
    // Sensitivity ? 0.061 mg/LSB = 0.000061 g/LSB
    return (float)raw * 0.000061f;
}

// Convert gyro raw LSB -> dps, for ±250 dps FS
float LSM6DSO_GyroLSB_to_dps(int16_t raw)
{
    // Sensitivity ? 8.75 mdps/LSB = 0.00875 dps/LSB
    return (float)raw * 0.00875f;
}

