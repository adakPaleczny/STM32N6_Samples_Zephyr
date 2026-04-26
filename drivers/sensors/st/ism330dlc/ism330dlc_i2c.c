/*
 * Copyright (c) 2025 STMicroelectronics
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr sensor driver for ISM330DLC 6-axis IMU (I2C bus).
 * Wraps the ST ISM330DLC BSP HAL using the Zephyr sensor API.
 */

#define DT_DRV_COMPAT st_ism330dlc

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "ism330dlc.h"

LOG_MODULE_REGISTER(ism330dlc, CONFIG_SENSOR_LOG_LEVEL);

/* Per-instance driver data (mutable) */
struct ism330dlc_data {
	ISM330DLC_Object_t obj;
	ISM330DLC_Axes_t   acc;   /* last fetched accel, in mg   */
	ISM330DLC_Axes_t   gyro;  /* last fetched gyro,  in mdps */
	const struct i2c_dt_spec *bus_dt;
};

/* Per-instance driver config (const, from devicetree) */
struct ism330dlc_config {
	struct i2c_dt_spec bus;
	int accel_odr;   /* Hz  */
	int accel_range; /* g   */
	int gyro_odr;    /* Hz  */
	int gyro_range;  /* dps */
};

/* ── Low-level I2C bus callbacks wired into the ST HAL context ───────────── */

static int32_t ism330dlc_i2c_read(void *handle, uint8_t reg,
				   uint8_t *buf, uint16_t len)
{
	const struct ism330dlc_data *data = (const struct ism330dlc_data *)handle;

	return i2c_burst_read_dt(data->bus_dt, reg, buf, len) == 0
		? ISM330DLC_OK : ISM330DLC_ERROR;
}

static int32_t ism330dlc_i2c_write(void *handle, uint8_t reg,
				    uint8_t *buf, uint16_t len)
{
	const struct ism330dlc_data *data = (const struct ism330dlc_data *)handle;

	return i2c_burst_write_dt(data->bus_dt, reg, buf, len) == 0
		? ISM330DLC_OK : ISM330DLC_ERROR;
}

/* ── Sensor API: sample_fetch ─────────────────────────────────────────────── */

static int ism330dlc_sample_fetch(const struct device *dev,
				  enum sensor_channel chan)
{
	struct ism330dlc_data *data = dev->data;

	bool fetch_acc  = (chan == SENSOR_CHAN_ALL ||
			   chan == SENSOR_CHAN_ACCEL_XYZ ||
			   chan == SENSOR_CHAN_ACCEL_X   ||
			   chan == SENSOR_CHAN_ACCEL_Y   ||
			   chan == SENSOR_CHAN_ACCEL_Z);

	bool fetch_gyro = (chan == SENSOR_CHAN_ALL ||
			   chan == SENSOR_CHAN_GYRO_XYZ ||
			   chan == SENSOR_CHAN_GYRO_X   ||
			   chan == SENSOR_CHAN_GYRO_Y   ||
			   chan == SENSOR_CHAN_GYRO_Z);

	if (!fetch_acc && !fetch_gyro) {
		return -ENOTSUP;
	}

	if (fetch_acc) {
		if (ISM330DLC_ACC_GetAxes(&data->obj, &data->acc) != ISM330DLC_OK) {
			LOG_ERR("Failed to read accelerometer");
			return -EIO;
		}
	}

	if (fetch_gyro) {
		if (ISM330DLC_GYRO_GetAxes(&data->obj, &data->gyro) != ISM330DLC_OK) {
			LOG_ERR("Failed to read gyroscope");
			return -EIO;
		}
	}

	return 0;
}

/* ── Unit conversion helpers ─────────────────────────────────────────────── */

/*
 * Convert mg (milli-g) to Zephyr sensor_value in m/s².
 * 1 mg = 9.80665e-3 m/s²  ≈  9807 µm/s²
 */
static void mg_to_sensor_value(int32_t mg, struct sensor_value *val)
{
	int64_t micro_ms2 = (int64_t)mg * 9807;

	val->val1 = (int32_t)(micro_ms2 / 1000000);
	val->val2 = (int32_t)(micro_ms2 % 1000000);
}

/*
 * Convert mdps (milli-degrees-per-second) to Zephyr sensor_value in rad/s.
 * 1 mdps = π/(180×1000) rad/s  ≈  17.453 µrad/s
 */
static void mdps_to_sensor_value(int32_t mdps, struct sensor_value *val)
{
	int64_t micro_rads = (int64_t)mdps * 17453 / 1000;

	val->val1 = (int32_t)(micro_rads / 1000000);
	val->val2 = (int32_t)(micro_rads % 1000000);
}

/* ── Sensor API: channel_get ──────────────────────────────────────────────── */

static int ism330dlc_channel_get(const struct device *dev,
				 enum sensor_channel chan,
				 struct sensor_value *val)
{
	const struct ism330dlc_data *data = dev->data;

	switch (chan) {
	case SENSOR_CHAN_ACCEL_X:
		mg_to_sensor_value(data->acc.x, val);
		break;
	case SENSOR_CHAN_ACCEL_Y:
		mg_to_sensor_value(data->acc.y, val);
		break;
	case SENSOR_CHAN_ACCEL_Z:
		mg_to_sensor_value(data->acc.z, val);
		break;
	case SENSOR_CHAN_ACCEL_XYZ:
		mg_to_sensor_value(data->acc.x, &val[0]);
		mg_to_sensor_value(data->acc.y, &val[1]);
		mg_to_sensor_value(data->acc.z, &val[2]);
		break;
	case SENSOR_CHAN_GYRO_X:
		mdps_to_sensor_value(data->gyro.x, val);
		break;
	case SENSOR_CHAN_GYRO_Y:
		mdps_to_sensor_value(data->gyro.y, val);
		break;
	case SENSOR_CHAN_GYRO_Z:
		mdps_to_sensor_value(data->gyro.z, val);
		break;
	case SENSOR_CHAN_GYRO_XYZ:
		mdps_to_sensor_value(data->gyro.x, &val[0]);
		mdps_to_sensor_value(data->gyro.y, &val[1]);
		mdps_to_sensor_value(data->gyro.z, &val[2]);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

/* ── Driver init ──────────────────────────────────────────────────────────── */

static int ism330dlc_init(const struct device *dev)
{
	struct ism330dlc_data *data      = dev->data;
	const struct ism330dlc_config *cfg = dev->config;
	uint8_t who_am_i;

	if (!device_is_ready(cfg->bus.bus)) {
		LOG_ERR("I2C bus '%s' not ready", cfg->bus.bus->name);
		return -ENODEV;
	}

	/* Store bus pointer so callbacks can reach it via 'handle' */
	data->bus_dt = &cfg->bus;

	/*
	 * Wire the ST HAL low-level context directly, bypassing
	 * ISM330DLC_RegisterBusIO so we can embed our own Zephyr callbacks
	 * without the detour through IO.ReadReg/WriteReg.
	 */
	data->obj.Ctx.read_reg  = ism330dlc_i2c_read;
	data->obj.Ctx.write_reg = ism330dlc_i2c_write;
	data->obj.Ctx.handle    = data;   /* passed back to callbacks */
	data->obj.IO.BusType    = ISM330DLC_I2C_BUS;
	data->obj.is_initialized = 0;

	/* Verify chip identity */
	if (ISM330DLC_ReadID(&data->obj, &who_am_i) != ISM330DLC_OK ||
	    who_am_i != ISM330DLC_ID) {
		LOG_ERR("WHO_AM_I mismatch: got 0x%02x, expected 0x%02x",
			who_am_i, ISM330DLC_ID);
		return -ENODEV;
	}

	/* Full sensor initialization (reset, BDU, FIFO bypass, ODR off) */
	if (ISM330DLC_Init(&data->obj) != ISM330DLC_OK) {
		LOG_ERR("ISM330DLC_Init failed");
		return -EIO;
	}

	/* Apply ODR and full-scale from devicetree, then enable */
	if (ISM330DLC_ACC_SetOutputDataRate(&data->obj,
					    (float)cfg->accel_odr) != ISM330DLC_OK ||
	    ISM330DLC_ACC_SetFullScale(&data->obj,
				      cfg->accel_range) != ISM330DLC_OK ||
	    ISM330DLC_ACC_Enable(&data->obj) != ISM330DLC_OK) {
		LOG_ERR("Failed to configure accelerometer");
		return -EIO;
	}

	if (ISM330DLC_GYRO_SetOutputDataRate(&data->obj,
					     (float)cfg->gyro_odr) != ISM330DLC_OK ||
	    ISM330DLC_GYRO_SetFullScale(&data->obj,
				       cfg->gyro_range) != ISM330DLC_OK ||
	    ISM330DLC_GYRO_Enable(&data->obj) != ISM330DLC_OK) {
		LOG_ERR("Failed to configure gyroscope");
		return -EIO;
	}

	LOG_INF("ISM330DLC at %s:0x%02x ready (acc %d Hz / %d g, gyro %d Hz / %d dps)",
		cfg->bus.bus->name, cfg->bus.addr,
		cfg->accel_odr, cfg->accel_range,
		cfg->gyro_odr,  cfg->gyro_range);

	return 0;
}

/* ── Driver API vtable ────────────────────────────────────────────────────── */

static const struct sensor_driver_api ism330dlc_driver_api = {
	.sample_fetch = ism330dlc_sample_fetch,
	.channel_get  = ism330dlc_channel_get,
};

/* ── Per-instance instantiation macro ────────────────────────────────────── */

#define ISM330DLC_DEFINE(inst)						\
	static struct ism330dlc_data ism330dlc_data_##inst;		\
									\
	static const struct ism330dlc_config ism330dlc_config_##inst = {\
		.bus         = I2C_DT_SPEC_INST_GET(inst),		\
		.accel_odr   = DT_INST_PROP_OR(inst, accel_odr,   104),\
		.accel_range = DT_INST_PROP_OR(inst, accel_range,   2),\
		.gyro_odr    = DT_INST_PROP_OR(inst, gyro_odr,    104),\
		.gyro_range  = DT_INST_PROP_OR(inst, gyro_range, 2000),\
	};								\
									\
	SENSOR_DEVICE_DT_INST_DEFINE(inst,				\
		ism330dlc_init, NULL,					\
		&ism330dlc_data_##inst, &ism330dlc_config_##inst,	\
		POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,		\
		&ism330dlc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ISM330DLC_DEFINE)
