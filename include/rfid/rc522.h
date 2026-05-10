/* Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INCLUDE_RFID_RC522_H
#define INCLUDE_RFID_RC522_H

#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rc522_uid {
	uint8_t data[10];
	uint8_t size;
};

/**
 * @brief Check whether a PICC is in the RF field.
 *
 * Sends a REQA short frame and checks for an ATQA response.
 *
 * @param dev RC522 device handle
 * @return 0 if a card was found, negative errno otherwise (-ETIMEDOUT = no card)
 */
int rc522_detect_card(const struct device *dev);

/**
 * @brief Run the ISO 14443-3 anti-collision loop and return the UID.
 *
 * Call after rc522_detect_card() succeeds.  Returns the first 4-byte
 * UID (single-size PICC cascade level 1).
 *
 * @param dev RC522 device handle
 * @param uid Output UID — uid.size is set to 4 on success
 * @return 0 on success, negative errno otherwise
 */
int rc522_get_uid(const struct device *dev, struct rc522_uid *uid);

/**
 * @brief Send HLTA to put the card into the HALT state.
 *
 * @param dev RC522 device handle
 * @return 0 always (HLTA expects no reply)
 */
int rc522_halt_card(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_RFID_RC522_H */
