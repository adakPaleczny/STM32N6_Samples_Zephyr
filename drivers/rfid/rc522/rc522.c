/* NXP MFRC522 RFID/NFC reader — Zephyr SPI driver
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Protocol reference: NXP MFRC522 datasheet Rev 3.9, ISO/IEC 14443-3.
 */

#define DT_DRV_COMPAT nxp_rc522

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <rfid/rc522.h>

LOG_MODULE_REGISTER(rc522, CONFIG_RC522_LOG_LEVEL);

/* ── Register map ───────────────────────────────────────────────────────── */
#define REG_COMMAND       0x01
#define REG_COM_IE        0x02
#define REG_COM_IRQ       0x04
#define REG_DIV_IRQ       0x05
#define REG_ERROR         0x06
#define REG_FIFO_DATA     0x09
#define REG_FIFO_LEVEL    0x0A
#define REG_BIT_FRAMING   0x0D
#define REG_COLL          0x0E
#define REG_MODE          0x11
#define REG_TX_MODE       0x12
#define REG_RX_MODE       0x13
#define REG_TX_CONTROL    0x14
#define REG_TX_ASK        0x15
#define REG_CRC_RESULT_H  0x21
#define REG_CRC_RESULT_L  0x22
#define REG_MOD_WIDTH     0x24
#define REG_T_MODE        0x2A
#define REG_T_PRESCALER   0x2B
#define REG_T_RELOAD_H    0x2C
#define REG_T_RELOAD_L    0x2D
#define REG_VERSION       0x37

/* ── RC522 chip commands ─────────────────────────────────────────────────── */
#define CMD_IDLE          0x00
#define CMD_CALC_CRC      0x03
#define CMD_TRANSMIT      0x04
#define CMD_TRANSCEIVE    0x0C
#define CMD_SOFT_RESET    0x0F

/* ── PICC (card) commands ────────────────────────────────────────────────── */
#define PICC_REQA         0x26
#define PICC_HLTA         0x50
#define PICC_SEL_CL1      0x93

/* ── ComIrqReg bits ─────────────────────────────────────────────────────── */
#define IRQ_TIMER         BIT(0)
#define IRQ_TX            BIT(6)
#define IRQ_RX            BIT(5)
#define IRQ_IDLE          BIT(4)

/* ── ErrorReg bits ──────────────────────────────────────────────────────── */
#define ERR_PROTO         BIT(0)
#define ERR_PARITY        BIT(1)
#define ERR_BUF_OVFL      BIT(4)
#define ERR_ANY           (ERR_PROTO | ERR_PARITY | ERR_BUF_OVFL)

/* ── Poll limits ────────────────────────────────────────────────────────── */
#define TRANSCEIVE_POLL_US  10
#define TRANSCEIVE_TIMEOUT  20000  /* iterations → ~200 ms */
#define CRC_POLL_US         10
#define CRC_TIMEOUT         10000  /* iterations → ~100 ms */
#define TX_POLL_US          100
#define TX_TIMEOUT          1000   /* iterations → ~100 ms */

struct rc522_config {
	struct spi_dt_spec spi;
};

struct rc522_data {
	/* no runtime state */
};

/* ── Register I/O ───────────────────────────────────────────────────────── */

/*
 * SPI address byte:
 *   write: bit7=0, bits[6:1]=reg, bit0=0  →  (reg << 1) & 0x7E
 *   read:  bit7=1, bits[6:1]=reg, bit0=0  →  ((reg << 1) & 0x7E) | 0x80
 */

static int reg_write(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct rc522_config *cfg = dev->config;
	uint8_t tx[2] = { (reg << 1) & 0x7E, val };
	const struct spi_buf tx_buf = { .buf = tx, .len = 2 };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(&cfg->spi, &tx_set);
}

static uint8_t reg_read(const struct device *dev, uint8_t reg)
{
	const struct rc522_config *cfg = dev->config;
	uint8_t tx[2] = { ((reg << 1) & 0x7E) | 0x80, 0x00 };
	uint8_t rx[2] = { 0 };
	const struct spi_buf tx_buf = { .buf = tx, .len = 2 };
	const struct spi_buf rx_buf = { .buf = rx, .len = 2 };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);
	return rx[1];
}

static void reg_set_bits(const struct device *dev, uint8_t reg, uint8_t mask)
{
	reg_write(dev, reg, reg_read(dev, reg) | mask);
}

static void reg_clear_bits(const struct device *dev, uint8_t reg, uint8_t mask)
{
	reg_write(dev, reg, reg_read(dev, reg) & ~mask);
}

static void fifo_flush(const struct device *dev)
{
	/* FlushBuffer: writing 0x80 to FIFOLevelReg clears the FIFO */
	reg_write(dev, REG_FIFO_LEVEL, 0x80);
}

/* ── CRC-A calculation (ISO 14443, preset 0x6363) ───────────────────────── */

static void calc_crc(const struct device *dev,
		     const uint8_t *data, uint8_t len,
		     uint8_t *lo, uint8_t *hi)
{
	reg_write(dev, REG_COMMAND, CMD_IDLE);
	reg_write(dev, REG_DIV_IRQ, 0x04);  /* clear CRCIRq */
	fifo_flush(dev);

	for (int i = 0; i < len; i++) {
		reg_write(dev, REG_FIFO_DATA, data[i]);
	}

	reg_write(dev, REG_COMMAND, CMD_CALC_CRC);

	for (int t = CRC_TIMEOUT; t > 0; t--) {
		/* DivIrqReg bit2 = CRCIRq */
		if (reg_read(dev, REG_DIV_IRQ) & BIT(2)) {
			break;
		}
		k_usleep(CRC_POLL_US);
	}

	*lo = reg_read(dev, REG_CRC_RESULT_L);
	*hi = reg_read(dev, REG_CRC_RESULT_H);
	reg_write(dev, REG_COMMAND, CMD_IDLE);
}

/* ── Core transceive (ISO 14443 half-duplex) ────────────────────────────── */

/*
 * @param tx_bits  Number of valid bits in the LAST byte of tx (0 = 8 bits).
 *                 Used for REQA (7 bits) and similar short frames.
 * @param rx       Buffer for received bytes, NULL to discard.
 * @param rx_len   In: capacity.  Out: number of bytes received.
 */
static int transceive(const struct device *dev,
		      const uint8_t *tx, uint8_t tx_len, uint8_t tx_bits,
		      uint8_t *rx, uint8_t *rx_len)
{
	reg_write(dev, REG_COMMAND, CMD_IDLE);
	reg_write(dev, REG_COM_IRQ, 0x7F);    /* clear all IRQ flags */
	reg_write(dev, REG_COM_IE, 0x77);     /* enable Rx/Idle/Timer IRQ */
	fifo_flush(dev);

	for (int i = 0; i < tx_len; i++) {
		reg_write(dev, REG_FIFO_DATA, tx[i]);
	}

	/* tx_bits in [2:0], StartSend in bit7 will be OR'd in next */
	reg_write(dev, REG_BIT_FRAMING, tx_bits & 0x07);
	reg_write(dev, REG_COMMAND, CMD_TRANSCEIVE);
	/* StartSend must be set AFTER writing the TRANSCEIVE command */
	reg_write(dev, REG_BIT_FRAMING, (tx_bits & 0x07) | 0x80);

	uint8_t irq = 0;

	for (int t = TRANSCEIVE_TIMEOUT; t > 0; t--) {
		irq = reg_read(dev, REG_COM_IRQ);
		if (irq & (IRQ_RX | IRQ_IDLE | IRQ_TIMER)) {
			break;
		}
		k_usleep(TRANSCEIVE_POLL_US);
	}

	reg_clear_bits(dev, REG_BIT_FRAMING, 0x80);  /* clear StartSend */

	if (irq & IRQ_TIMER) {
		return -ETIMEDOUT;
	}
	if (!(irq & (IRQ_RX | IRQ_IDLE))) {
		return -ETIMEDOUT;
	}

	uint8_t err = reg_read(dev, REG_ERROR);

	if (err & ERR_ANY) {
		LOG_DBG("transceive error 0x%02x", err);
		return -EIO;
	}

	if (rx && rx_len) {
		uint8_t n = reg_read(dev, REG_FIFO_LEVEL);

		n = MIN(n, *rx_len);
		*rx_len = n;
		for (int i = 0; i < n; i++) {
			rx[i] = reg_read(dev, REG_FIFO_DATA);
		}
	}

	return 0;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int rc522_detect_card(const struct device *dev)
{
	/* Reset framing registers in case previous op left them dirty */
	reg_write(dev, REG_TX_MODE, 0x00);
	reg_write(dev, REG_RX_MODE, 0x00);
	reg_write(dev, REG_MOD_WIDTH, 0x26);

	/* REQA is a 7-bit short frame */
	uint8_t cmd = PICC_REQA;
	uint8_t atqa[2];
	uint8_t atqa_len = sizeof(atqa);

	int ret = transceive(dev, &cmd, 1, 7, atqa, &atqa_len);

	if (ret < 0) {
		return ret;
	}

	return (atqa_len == 2) ? 0 : -ENODEV;
}

int rc522_get_uid(const struct device *dev, struct rc522_uid *uid)
{
	/* ISO 14443-3 anti-collision: SEL CL1, NVB=0x20 (0 uid bits known) */
	uint8_t buf[2] = { PICC_SEL_CL1, 0x20 };
	uint8_t rx[5];
	uint8_t rx_len = sizeof(rx);

	/* Disable collision halting so we always get all 5 bytes */
	reg_write(dev, REG_COLL, 0x80);

	int ret = transceive(dev, buf, 2, 0, rx, &rx_len);

	if (ret < 0) {
		return ret;
	}

	if (rx_len != 5) {
		LOG_DBG("anticoll: expected 5 bytes, got %d", rx_len);
		return -EIO;
	}

	/* BCC check: XOR of UID bytes must equal the check byte */
	if ((rx[0] ^ rx[1] ^ rx[2] ^ rx[3]) != rx[4]) {
		LOG_DBG("anticoll: BCC mismatch");
		return -EBADMSG;
	}

	uid->size = 4;
	memcpy(uid->data, rx, 4);

	return 0;
}

int rc522_halt_card(const struct device *dev)
{
	/* HLTA = [0x50, 0x00, CRC_A_lo, CRC_A_hi] — no reply expected */
	uint8_t buf[4];

	buf[0] = PICC_HLTA;
	buf[1] = 0x00;
	calc_crc(dev, buf, 2, &buf[2], &buf[3]);

	reg_write(dev, REG_COMMAND, CMD_IDLE);
	reg_write(dev, REG_COM_IRQ, 0x7F);
	fifo_flush(dev);

	for (int i = 0; i < 4; i++) {
		reg_write(dev, REG_FIFO_DATA, buf[i]);
	}

	reg_write(dev, REG_COMMAND, CMD_TRANSMIT);

	/* Wait for TxIRq — TRANSMIT starts immediately without StartSend */
	for (int t = TX_TIMEOUT; t > 0; t--) {
		if (reg_read(dev, REG_COM_IRQ) & IRQ_TX) {
			break;
		}
		k_usleep(TX_POLL_US);
	}

	reg_write(dev, REG_COMMAND, CMD_IDLE);
	return 0;
}

/* ── Driver init ────────────────────────────────────────────────────────── */

static int rc522_init(const struct device *dev)
{
	const struct rc522_config *cfg = dev->config;

	if (!spi_is_ready_dt(&cfg->spi)) {
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}

	/* Soft-reset: all registers return to default values */
	reg_write(dev, REG_COMMAND, CMD_SOFT_RESET);
	k_msleep(50);

	uint8_t ver = reg_read(dev, REG_VERSION);

	LOG_INF("RC522 chip version 0x%02x", ver);
	if (ver != 0x91 && ver != 0x92) {
		LOG_WRN("unexpected version — wiring or CS issue?");
	}

	/*
	 * Timer: auto-start after each transmission ends.
	 * f_timer = 13.56 MHz / (2 * (TPrescaler + 1)) = ~39.88 kHz
	 * TReload = 1000  →  timeout ≈ 25 ms  (enough for 14443-3 T1 window)
	 */
	reg_write(dev, REG_T_MODE,      0x80);  /* TAuto = 1 */
	reg_write(dev, REG_T_PRESCALER, 0xA9);  /* TPrescaler = 169 */
	reg_write(dev, REG_T_RELOAD_H,  0x03);
	reg_write(dev, REG_T_RELOAD_L,  0xE8);  /* TReload = 1000 */

	/* Force 100% ASK modulation (required for ISO 14443-A) */
	reg_write(dev, REG_TX_ASK, 0x40);

	/* CRC preset = 0x6363 (ISO 14443-A CRC_A initial value) */
	reg_write(dev, REG_MODE, 0x3D);

	/* Enable Tx1 and Tx2 antenna drivers */
	reg_set_bits(dev, REG_TX_CONTROL, 0x03);

	return 0;
}

/* ── Device instance macro ──────────────────────────────────────────────── */

#define RC522_INIT(n)                                                       \
	static const struct rc522_config rc522_config_##n = {              \
		.spi = SPI_DT_SPEC_INST_GET(n,                              \
			SPI_OP_MODE_MASTER | SPI_WORD_SET(8) |              \
			SPI_TRANSFER_MSB, 0),                               \
	};                                                                  \
	static struct rc522_data rc522_data_##n;                           \
	DEVICE_DT_INST_DEFINE(n, rc522_init, NULL,                         \
		&rc522_data_##n, &rc522_config_##n,                        \
		POST_KERNEL, CONFIG_SPI_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(RC522_INIT)
