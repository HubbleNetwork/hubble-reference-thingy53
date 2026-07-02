/*
 * Copyright (c) 2026 HubbleNetwork
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LED_H
#define LED_H

#ifdef CONFIG_APP_BLINK_LED

/**
 * @brief Start blinking the LED
 *
 * Start blinking the LED. The LED selection depends on Kconfig: led1 for
 * CONFIG_APP_LED_GREEN, led2 for CONFIG_APP_LED_BLUE, or led0 otherwise.
 *
 * @retval 0 on success.
 * @retval -EAGAIN if the device is not ready.
 * @retval negative errno on GPIO configuration failure.
 */
int led_blink_start(void);

/**
 * @brief Stop blinking the LED and turn it off.
 *
 * Stops blinking the LED.
 */
void led_blink_stop(void);

#else

static inline int led_blink_start(void)
{
	return 0;
}

static inline void led_blink_stop(void)
{
}

#endif	/* CONFIG_APP_BLINK_LED */

#endif 	/* LED_H  */
