/***************************************************************************
* Copyright (C) 2015 by Alexander G <pidbip@gmail.com>                     *
* Copyright (C) 2019 by Kyle Guinn <elyk03@gmail.com>                      *
*                                                                          *
* This program is free software: you can redistribute it and/or modify     *
* it under the terms of the GNU General Public License as published by     *
* the Free Software Foundation, either version 3 of the License, or        *
* (at your option) any later version.                                      *
*                                                                          *
* This program is distributed in the hope that it will be useful,          *
* but WITHOUT ANY WARRANTY; without even the implied warranty of           *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the             *
* GNU General Public License for more details.                             *
*                                                                          *
* You should have received a copy of the GNU General Public License        *
* along with this program. If not, see <http://www.gnu.org/licenses/>.     *
***************************************************************************/

#ifndef THERMAPP_H_
#define THERMAPP_H_

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include <libusb.h>

#define VENDOR  0x1772
#define PRODUCT 0x0002

#define TRANSFER_SIZE 8192
#if (TRANSFER_SIZE % 512) || (TRANSFER_SIZE < 512)
#error TRANSFER_SIZE must be a multiple of 512
#endif

#define FRAME_WIDTH  384
#define FRAME_HEIGHT 288
#define PIXELS_DATA_SIZE (FRAME_WIDTH * FRAME_HEIGHT)

// AD5628 DAC in Therm App is for generating control voltage
// VREF = 2.5 volts 11 Bit
struct cfg_packet {
	uint16_t preamble[4];
	uint16_t modes;// 0xXXXM  Modes set last nibble
	uint16_t serial_num_lo;
	uint16_t serial_num_hi;
	uint16_t hardware_ver;
	uint16_t firmware_ver;
	uint16_t data_09;
	uint16_t data_0a;
	uint16_t data_0b;
	uint16_t data_0c;
	uint16_t data_0d;
	uint16_t data_0e;
	int16_t temperature;
	uint16_t VoutA; //DCoffset;// AD5628 VoutA, Range: 0V - 2.45V, max 2048
	uint16_t data_11;
	uint16_t VoutC;//gain;// AD5628 VoutC, Range: 0V - 3.59V, max 2984 ??????
	uint16_t VoutD;// AD5628 VoutD, Range: 0V - 2.895V, max 2394 ??????
	uint16_t VoutE;// AD5628 VoutE, Range: 0V - 3.63V, max 2997, FPA VBUS
	uint16_t data_15;
	uint16_t data_16;
	uint16_t data_17;
	uint16_t data_18;
	uint16_t data_19;
	uint16_t frame_count;
	uint16_t data_1b;
	uint16_t data_1c;
	uint16_t data_1d;
	uint16_t data_1e;
	uint16_t data_1f;
};

struct thermapp_packet {
	struct cfg_packet header;
	int16_t pixels_data[PIXELS_DATA_SIZE];
};

typedef struct thermapp {
	libusb_context *ctx;
	libusb_device_handle *dev;
	struct libusb_transfer *transfer_in;
	struct libusb_transfer *transfer_out;

	int started_read_async;
	pthread_t pthread_read_async;
	pthread_mutex_t mutex_getimage;
	pthread_cond_t cond_getimage;
	int complete;

	struct cfg_packet *cfg;
	struct thermapp_packet *data_in;
	struct thermapp_packet *data_done;
	uint32_t serial_num;
	uint16_t hardware_ver;
	uint16_t firmware_ver;
	int16_t temperature;
	uint16_t frame_count;
} ThermApp;


ThermApp *thermapp_open(void);
int thermapp_usb_connect(ThermApp *thermapp);
int thermapp_thread_create(ThermApp *thermapp);
int thermapp_close(ThermApp *thermapp);

int thermapp_getImage(ThermApp *thermapp, int16_t *ImgData);
uint32_t thermapp_getSerialNumber(ThermApp *thermapp);
uint16_t thermapp_getHardwareVersion(ThermApp *thermapp);
uint16_t thermapp_getFirmwareVersion(ThermApp *thermapp);
float thermapp_getTemperature(ThermApp *thermapp);
uint16_t thermapp_getFrameCount(ThermApp *thermapp);

#endif /* THERMAPP_H_ */
