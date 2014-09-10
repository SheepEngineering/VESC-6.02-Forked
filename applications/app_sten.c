/*
	Copyright 2012-2014 Benjamin Vedder	benjamin@vedder.se

	This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

/*
 * app_sten.c
 *
 *  Created on: 18 apr 2014
 *      Author: benjamin
 */

#include "app.h"
#ifdef USE_APP_STEN

#include "ch.h"
#include "hal.h"
#include "stm32f4xx_conf.h"
#include "mcpwm.h"
#include "utils.h"
#include "hw.h"
#include <math.h>

// Settings
#define TIMEOUT		500
#define HYST		0.10
// 29000rpm = 20kmh
#define RPM_MAX_1	41000.0	// Start decreasing output here
#define RPM_MAX_2	44000.0	// Completely stop output here

// Private variables
static volatile float out_received = 0.0;

// Threads
static msg_t uart_thread(void *arg);
static WORKING_AREA(uart_thread_wa, 1024);
static volatile systime_t last_uart_update_time;

// Private functions
static void set_output(float output);
static uint16_t middle_of_3(uint16_t a, uint16_t b, uint16_t c);

/*
 * This callback is invoked when a transmission buffer has been completely
 * read by the driver.
 */
static void txend1(UARTDriver *uartp) {
	(void)uartp;
}

/*
 * This callback is invoked when a transmission has physically completed.
 */
static void txend2(UARTDriver *uartp) {
	(void)uartp;
}

/*
 * This callback is invoked on a receive error, the errors mask is passed
 * as parameter.
 */
static void rxerr(UARTDriver *uartp, uartflags_t e) {
	(void)uartp;
	(void)e;
}

/*
 * This callback is invoked when a character is received but the application
 * was not ready to receive it, the character is passed as parameter.
 */
static void rxchar(UARTDriver *uartp, uint16_t c) {
	(void)uartp;

	static uint16_t c1 = 128;
	static uint16_t c2 = 128;

	uint16_t med = middle_of_3(c, c1, c2);

	c2 = c1;
	c1 = c;

	out_received = ((float)med / 128) - 1.0;
	last_uart_update_time = chTimeNow();
}

/*
 * This callback is invoked when a receive buffer has been completely written.
 */
static void rxend(UARTDriver *uartp) {
	(void)uartp;
}

/*
 * UART driver configuration structure.
 */
static UARTConfig uart_cfg = {
		txend1,
		txend2,
		rxend,
		rxchar,
		rxerr,
		9600,
		0,
		USART_CR2_LINEN,
		0
};

void app_sten_init(void) {
	chThdCreateStatic(uart_thread_wa, sizeof(uart_thread_wa), NORMALPRIO - 1, uart_thread, NULL);
}

static msg_t uart_thread(void *arg) {
	(void)arg;

	chRegSetThreadName("UART");

	uartStart(&HW_UART_DEV, &uart_cfg);
	palSetPadMode(HW_UART_TX_PORT, HW_UART_TX_PIN, PAL_MODE_ALTERNATE(HW_UART_GPIO_AF) |
			PAL_STM32_OSPEED_HIGHEST |
			PAL_STM32_PUDR_PULLUP);
	palSetPadMode(HW_UART_RX_PORT, HW_UART_RX_PIN, PAL_MODE_ALTERNATE(HW_UART_GPIO_AF) |
			PAL_STM32_OSPEED_HIGHEST |
			PAL_STM32_PUDR_PULLUP);

	systime_t time = chTimeNow();

	for(;;) {
		time += MS2ST(1);

		if ((systime_t) ((float) chTimeElapsedSince(last_uart_update_time)
				/ ((float) CH_FREQUENCY / 1000.0)) > (float)TIMEOUT) {
			mcpwm_set_brake_current(-10.0);
		} else {
			set_output(out_received);
		}

		chThdSleepUntil(time);
	}

	return 0;
}

static void set_output(float output) {
	output /= (1.0 - HYST);

	if (output > HYST) {
		output -= HYST;
	} else if (output < -HYST) {
		output += HYST;
	} else {
		output = 0.0;
	}

	const float rpm = mcpwm_get_rpm();

	if (output > 0.0 && rpm > -MCPWM_MIN_RPM) {
		float current = output * MCPWM_CURRENT_MAX;

		// Soft RPM limit
		if (rpm > RPM_MAX_2) {
			current = -MCPWM_CURRENT_CONTROL_MIN;
		} else if (rpm > RPM_MAX_1) {
			current = utils_map(rpm, RPM_MAX_1, RPM_MAX_2, current, -MCPWM_CURRENT_CONTROL_MIN);
		}

		// Some low-pass filtering
		static float current_p1 = 0.0;
		static float current_p2 = 0.0;
		current = (current + current_p1 + current_p2) / 3;
		current_p2 = current_p1;
		current_p1 = current;

		if (fabsf(current) < MCPWM_CURRENT_CONTROL_MIN) {
			current = -MCPWM_CURRENT_CONTROL_MIN;
		}

		mcpwm_set_current(current);
	} else {
		mcpwm_set_brake_current(output * MCPWM_CURRENT_MIN);
	}
}

static uint16_t middle_of_3(uint16_t a, uint16_t b, uint16_t c) {
	uint16_t middle;

	if ((a <= b) && (a <= c)) {
		middle = (b <= c) ? b : c;
	} else if ((b <= a) && (b <= c)) {
		middle = (a <= c) ? a : c;
	} else {
		middle = (a <= b) ? a : b;
	}
	return middle;
}

#endif
