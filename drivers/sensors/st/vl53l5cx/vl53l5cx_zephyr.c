/*
 * Copyright (c) 2025 STMicroelectronics
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr sensor driver for VL53L5CX multi-zone ToF ranging sensor (I2C bus).
 * Wraps the ST VL53L5CX BSP / ULD using the Zephyr sensor API.
 *
 * sensor_channel_get(SENSOR_CHAN_DISTANCE) → zone 0, target 0 in meters.
 * All zone results are accessible via driver data for custom use.
 *
 * Required files in this directory (BSP + ULD from ST):
 *   vl53l5cx.c / vl53l5cx.h           – BSP component driver
 *   vl53l5cx_api.c / vl53l5cx_api.h   – ULD core API
 *   vl53l5cx_plugin_xtalk.c/h         – ULD xtalk plugin
 *   vl53l5cx_plugin_detection_thresholds.c/h – ULD threshold plugin
 */

#define DT_DRV_COMPAT st_vl53l5cx

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "vl53l5cx.h"

LOG_MODULE_REGISTER(vl53l5cx, CONFIG_SENSOR_LOG_LEVEL);

/* ── Per-instance structs ────────────────────────────────────────────────── */

struct vl53l5cx_data {
	VL53L5CX_Object_t obj;
	VL53L5CX_Result_t result;      /* last fetched zone results             */
	const struct i2c_dt_spec *bus; /* set in init, used by static callbacks */
};

struct vl53l5cx_config {
	struct i2c_dt_spec bus;
	uint8_t  ranging_profile; /* VL53L5CX_PROFILE_* constant             */
	uint32_t timing_budget;   /* ms                                      */
	uint32_t frequency;       /* Hz                                      */
};

/*
 * Module-level bus pointer for single-instance use.
 * The BSP ReadReg/WriteReg callbacks carry no handle, so we keep a global.
 * Marked non-static so platform.c can access it for I2C operations.
 */
const struct i2c_dt_spec *vl53_bus;

/* ── I2C platform callbacks ──────────────────────────────────────────────── */

/*
 * VL53L5CX uses 16-bit register addresses. Zephyr's i2c_burst_read/write
 * only support 8-bit register addresses, so we build explicit I2C messages.
 *
 * addr: 8-bit I2C address (as stored by BSP, e.g. 0x52).
 * reg:  16-bit register address.
 */

#define VL53L5CX_PLAT_WRITE_CHUNK 256U

static int32_t vl53l5cx_plat_write(uint16_t addr, uint16_t reg,
				    uint8_t *buf, uint16_t len)
{
	static uint8_t chunk[2U + VL53L5CX_PLAT_WRITE_CHUNK];
	uint16_t offset = 0;

	do {
		uint16_t sz = (uint16_t)((len - offset) < (uint16_t)VL53L5CX_PLAT_WRITE_CHUNK
					  ? (len - offset) : (uint16_t)VL53L5CX_PLAT_WRITE_CHUNK);
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
			return VL53L5CX_ERROR;
		}

		offset += sz;
	} while (offset < len);

	return VL53L5CX_OK;
}

static int32_t vl53l5cx_plat_read(uint16_t addr, uint16_t reg,
				   uint8_t *buf, uint16_t len)
{
	uint8_t reg_buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
	struct i2c_msg msgs[2] = {
		{ .buf = reg_buf, .len = 2,   .flags = I2C_MSG_WRITE },
		{ .buf = buf,     .len = len, .flags = I2C_MSG_READ | I2C_MSG_RESTART | I2C_MSG_STOP },
	};

	return i2c_transfer(vl53_bus->bus, msgs, 2, addr >> 1) == 0
		? VL53L5CX_OK : VL53L5CX_ERROR;
}

static int32_t vl53l5cx_plat_init(void)
{
	return device_is_ready(vl53_bus->bus) ? VL53L5CX_OK : VL53L5CX_ERROR;
}

static int32_t vl53l5cx_plat_deinit(void)
{
	return VL53L5CX_OK;
}

static int32_t vl53l5cx_plat_get_tick(void)
{
	return (int32_t)k_uptime_get_32();
}

/* ── Sensor API: sample_fetch ────────────────────────────────────────────── */

static int vl53l5cx_sample_fetch(const struct device *dev,
				  enum sensor_channel chan)
{
	struct vl53l5cx_data *data = dev->data;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_DISTANCE) {
		return -ENOTSUP;
	}

	if (VL53L5CX_GetDistance(&data->obj, &data->result) != VL53L5CX_OK) {
		LOG_ERR("VL53L5CX_GetDistance failed");
		return -EIO;
	}

	return 0;
}

/* ── Unit conversion ─────────────────────────────────────────────────────── */

/*
 * Convert millimetres to Zephyr sensor_value in metres.
 * val1 = whole metres, val2 = fractional part in micro-metres.
 */
static void mm_to_sensor_value(uint32_t mm, struct sensor_value *val)
{
	val->val1 = (int32_t)(mm / 1000U);
	val->val2 = (int32_t)((mm % 1000U) * 1000U);
}

/* ── Sensor API: channel_get ─────────────────────────────────────────────── */

static int vl53l5cx_channel_get(const struct device *dev,
				 enum sensor_channel chan,
				 struct sensor_value *val)
{
	const struct vl53l5cx_data *data = dev->data;

	if (chan != SENSOR_CHAN_DISTANCE) {
		return -ENOTSUP;
	}

	if (data->result.NumberOfZones == 0U) {
		return -ENODATA;
	}

	/* Expose zone 0, target 0. Status 0 means valid measurement. */
	if (data->result.ZoneResult[0].Status[0] != 0U) {
		LOG_WRN("Zone 0 status NOK: %u",
			data->result.ZoneResult[0].Status[0]);
	}

	mm_to_sensor_value(data->result.ZoneResult[0].Distance[0], val);

	return 0;
}

/* ── Driver init ─────────────────────────────────────────────────────────── */

static int vl53l5cx_zephyr_init(const struct device *dev)
{
	struct vl53l5cx_data *data       = dev->data;
	const struct vl53l5cx_config *cfg = dev->config;
	uint32_t sensor_id               = 0;
	uint16_t addr7                   = cfg->bus.addr;

	if (!device_is_ready(cfg->bus.bus)) {
		LOG_ERR("I2C bus '%s' not ready", cfg->bus.bus->name);
		return -ENODEV;
	}

	/* Store bus pointer for module-level callbacks */
	data->bus = &cfg->bus;
	vl53_bus  = &cfg->bus;

	if (addr7 > 0x7F) {
		LOG_WRN("VL53L5CX DTS reg uses 8-bit addr 0x%02x, normalizing to 7-bit 0x%02x",
			addr7, addr7 >> 1);
		addr7 >>= 1;
	}

	VL53L5CX_IO_t io = {
		.Init     = vl53l5cx_plat_init,
		.DeInit   = vl53l5cx_plat_deinit,
		.Address  = (uint16_t)(addr7 << 1), /* 7-bit → 8-bit BSP format */
		.WriteReg = vl53l5cx_plat_write,
		.ReadReg  = vl53l5cx_plat_read,
		.GetTick  = vl53l5cx_plat_get_tick,
	};

	if (VL53L5CX_RegisterBusIO(&data->obj, &io) != VL53L5CX_OK) {
		LOG_ERR("VL53L5CX_RegisterBusIO failed");
		return -EIO;
	}

	/* Initialize platform address in the configuration structure.
	 * This is used by platform.c functions to identify the I2C address.
	 */
	data->obj.Dev.platform.address = (uint16_t)(addr7 << 1);

	if (VL53L5CX_ReadID(&data->obj, &sensor_id) != VL53L5CX_OK ||
	    sensor_id != VL53L5CX_ID) {
		LOG_ERR("WHO_AM_I mismatch: got 0x%04x, expected 0x%04x",
			sensor_id, VL53L5CX_ID);
		return -ENODEV;
	}

	/* Firmware upload + sensor initialization (~500 ms) */
	if (VL53L5CX_Init(&data->obj) != VL53L5CX_OK) {
		LOG_ERR("VL53L5CX_Init failed");
		return -EIO;
	}

	VL53L5CX_ProfileConfig_t profile = {
		.RangingProfile = cfg->ranging_profile,
		.TimingBudget   = cfg->timing_budget,
		.Frequency      = cfg->frequency,
		.EnableAmbient  = 0U,
		.EnableSignal   = 0U,
	};

	if (VL53L5CX_ConfigProfile(&data->obj, &profile) != VL53L5CX_OK) {
		LOG_ERR("VL53L5CX_ConfigProfile failed");
		return -EIO;
	}

	if (VL53L5CX_Start(&data->obj, VL53L5CX_MODE_BLOCKING_CONTINUOUS)
	    != VL53L5CX_OK) {
		LOG_ERR("VL53L5CX_Start failed");
		return -EIO;
	}

	LOG_INF("VL53L5CX at %s:0x%02x ready (profile %u, budget %u ms, %u Hz)",
		cfg->bus.bus->name, addr7,
		cfg->ranging_profile, cfg->timing_budget, cfg->frequency);

	return 0;
}

/* ── Driver API vtable ───────────────────────────────────────────────────── */

static const struct sensor_driver_api vl53l5cx_driver_api = {
	.sample_fetch = vl53l5cx_sample_fetch,
	.channel_get  = vl53l5cx_channel_get,
};

/* ── Per-instance instantiation macro ───────────────────────────────────── */

#define VL53L5CX_DEFINE(inst)						\
	static struct vl53l5cx_data vl53l5cx_data_##inst;		\
									\
	static const struct vl53l5cx_config vl53l5cx_config_##inst = {	\
		.bus             = I2C_DT_SPEC_INST_GET(inst),		\
		.ranging_profile = DT_INST_PROP_OR(inst, ranging_profile,\
					VL53L5CX_PROFILE_4x4_CONTINUOUS),\
		.timing_budget   = DT_INST_PROP_OR(inst, timing_budget, 30),\
		.frequency       = DT_INST_PROP_OR(inst, frequency,     5),\
	};								\
									\
	SENSOR_DEVICE_DT_INST_DEFINE(inst,				\
		vl53l5cx_zephyr_init, NULL,					\
		&vl53l5cx_data_##inst, &vl53l5cx_config_##inst,	\
		POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,		\
		&vl53l5cx_driver_api);

DT_INST_FOREACH_STATUS_OKAY(VL53L5CX_DEFINE)
