#include "common.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "can.h"
#include "can_common.h"
#include "config.h"
#include "device.h"
#include "dfu.h"
#include "gpio.h"
#include "gs_usb.h"
#include "hal_include.h"
#include "led.h"
#include "timer.h"
#include "usbd_conf.h"
#include "usbd_core.h"
#include "usbd_def.h"
#include "usbd_desc.h"
#include "usbd_gs_can.h"
#include "util.h"

static USBD_GS_CAN_HandleTypeDef hGS_CAN;
static USBD_HandleTypeDef hUSB = {0};

void Main()
{
	gpio_init();
	timer_init();

	INIT_LIST_HEAD(&hGS_CAN.list_frame_pool);
	INIT_LIST_HEAD(&hGS_CAN.list_to_host);

	for (unsigned i = 0; i < ARRAY_SIZE(hGS_CAN.msgbuf); i++) {
		list_add_tail(&hGS_CAN.msgbuf[i].list, &hGS_CAN.list_frame_pool);
	}

	for (unsigned int i = 0; i < ARRAY_SIZE(hGS_CAN.channels); i++) {
		can_data_t *channel = &hGS_CAN.channels[i];

		channel->nr = i;

		INIT_LIST_HEAD(&channel->list_from_host);

		led_init(&channel->leds,
				 LEDRX_GPIO_Port, LEDRX_Pin, LEDRX_Active_High,
				 LEDTX_GPIO_Port, LEDTX_Pin, LEDTX_Active_High);

		/* nice wake-up pattern */
		for (uint8_t j = 0; j < 10; j++) {
			HAL_GPIO_TogglePin(LEDRX_GPIO_Port, LEDRX_Pin);
			HAL_Delay(50);
			HAL_GPIO_TogglePin(LEDTX_GPIO_Port, LEDTX_Pin);
		}

		led_set_mode(&channel->leds, LED_MODE_OFF);

		can_init(channel, CAN_INTERFACE);
		can_disable(channel);

#ifdef CAN_S_GPIO_Port
		HAL_GPIO_WritePin(CAN_S_GPIO_Port, CAN_S_Pin, GPIO_PIN_RESET);
#endif
	}

	USBD_Init(&hUSB, (USBD_DescriptorsTypeDef*)&FS_Desc, DEVICE_FS);
	USBD_RegisterClass(&hUSB, &USBD_GS_CAN);
	USBD_GS_CAN_Init(&hGS_CAN, &hUSB);
	USBD_Start(&hUSB);

	while (1) {
		for (unsigned int i = 0; i < ARRAY_SIZE(hGS_CAN.channels); i++) {
			can_data_t *channel = &hGS_CAN.channels[i];

			CAN_SendFrame(&hGS_CAN, channel);
		}

		USBD_GS_CAN_ReceiveFromHost(&hUSB);
		USBD_GS_CAN_SendToHost(&hUSB);

		for (unsigned int i = 0; i < ARRAY_SIZE(hGS_CAN.channels); i++) {
			can_data_t *channel = &hGS_CAN.channels[i];

			CAN_ReceiveFrame(&hGS_CAN, channel);
			CAN_HandleError(&hGS_CAN, channel);

			led_update(&channel->leds);
		}

		if (USBD_GS_CAN_DfuDetachRequested(&hUSB)) {
			dfu_run_bootloader();
		}
	}
}