/**
  ******************************************************************************
  * @file    platform.c
  * @author  IMG SW Application Team / Zephyr Adaptation
  * @brief   Platform functions for VL53L5CX ULD - Zephyr I2C integration
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  * Adapted for Zephyr RTOS: Platform functions use Zephyr I2C API directly.
  * The vl53_bus pointer is set by the Zephyr driver (vl53l5cx_zephyr.c).
  *
  ******************************************************************************
  */

#include "platform.h"
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

/* Declared and set by the Zephyr driver (vl53l5cx_zephyr.c) */
extern const struct i2c_dt_spec *vl53_bus;

/**
 * Read a byte from VL53L5CX register (16-bit addressing)
 * addr: I2C slave address (8-bit format, e.g. 0x52)
 * reg:  16-bit register address
 * Returns: 0 on success, nonzero on error
 */
static int32_t vl53_i2c_read(uint16_t addr, uint16_t reg,
                             uint8_t *buf, uint16_t len)
{
  uint8_t reg_buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
  struct i2c_msg msgs[2] = {
    { .buf = reg_buf, .len = 2,   .flags = I2C_MSG_WRITE },
    { .buf = buf,     .len = len, .flags = I2C_MSG_READ | I2C_MSG_RESTART | I2C_MSG_STOP },
  };

  if (!vl53_bus) {
    return 1;  /* Bus not initialized */
  }

  return i2c_transfer(vl53_bus->bus, msgs, 2, addr >> 1) == 0 ? 0 : 1;
}

/*
 * Maximum data bytes per I2C write transaction (excluding the 2-byte reg addr).
 * Keeps total message size at 258 bytes — safe for all STM32 I2C transfers.
 * Large writes (e.g. 32 KB firmware upload) are split into chunks; the register
 * address is incremented by the chunk size each iteration so the sensor's
 * auto-increment addressing remains correct.
 */
#define VL53L5CX_WRITE_CHUNK  256U

/**
 * Write bytes to VL53L5CX register (16-bit addressing).
 * addr: I2C slave address (8-bit format, e.g. 0x52)
 * reg:  16-bit register address
 * Returns: 0 on success, nonzero on error
 *
 * Each chunk is sent as a SINGLE i2c_msg (reg_hi + reg_lo + data) to avoid the
 * STM32 I2C v2 driver inserting a STOP between a two-message write sequence.
 */
static int32_t vl53_i2c_write(uint16_t addr, uint16_t reg,
                              uint8_t *buf, uint16_t len)
{
  static uint8_t chunk[2U + VL53L5CX_WRITE_CHUNK];
  uint16_t offset = 0;

  if (!vl53_bus) {
    return 1;
  }

  do {
    uint16_t sz = (uint16_t)((len - offset) < (uint16_t)VL53L5CX_WRITE_CHUNK
                              ? (len - offset) : (uint16_t)VL53L5CX_WRITE_CHUNK);
    uint16_t cur_reg = reg + offset;

    chunk[0] = (uint8_t)(cur_reg >> 8);
    chunk[1] = (uint8_t)(cur_reg & 0xFFU);
    memcpy(&chunk[2], buf + offset, sz);

    struct i2c_msg msg = {
      .buf   = chunk,
      .len   = 2U + sz,
      .flags = I2C_MSG_WRITE | I2C_MSG_STOP,
    };

    if (i2c_transfer(vl53_bus->bus, &msg, 1, addr >> 1) != 0) {
      return 1;
    }

    offset += sz;
  } while (offset < len);

  return 0;
}

/**
 * ULD platform functions: implemented using Zephyr I2C API
 */

uint8_t RdByte(VL53L5CX_Platform *p_platform, uint16_t RegisterAdress,
               uint8_t *p_value)
{
  return vl53_i2c_read(p_platform->address, RegisterAdress, p_value, 1U);
}

uint8_t WrByte(VL53L5CX_Platform *p_platform, uint16_t RegisterAdress,
               uint8_t value)
{
  return vl53_i2c_write(p_platform->address, RegisterAdress, &value, 1U);
}

uint8_t WrMulti(VL53L5CX_Platform *p_platform, uint16_t RegisterAdress,
                uint8_t *p_values, uint32_t size)
{
  return vl53_i2c_write(p_platform->address, RegisterAdress, p_values, size);
}

uint8_t RdMulti(VL53L5CX_Platform *p_platform, uint16_t RegisterAdress,
                uint8_t *p_values, uint32_t size)
{
  return vl53_i2c_read(p_platform->address, RegisterAdress, p_values, size);
}

void SwapBuffer(uint8_t *buffer, uint16_t size)
{
  uint32_t i, tmp;

  /* Swap buffer for endianness (little ↔ big endian conversion) */
  for (i = 0; i < size; i = i + 4) {
    tmp = ((buffer[i] << 24) | (buffer[i + 1] << 16) |
           (buffer[i + 2] << 8) | (buffer[i + 3]));

    __builtin_memcpy(&(buffer[i]), &tmp, 4);
  }
}

uint8_t WaitMs(VL53L5CX_Platform *p_platform, uint32_t TimeMs)
{
  int32_t tickstart = (int32_t)k_uptime_get_32();

  /* Poll until timeout */
  while (((int32_t)k_uptime_get_32() - tickstart) < (int32_t)TimeMs) {
    k_usleep(100);  /* Avoid busy-wait; yield periodically */
  }

  return 0;
}

