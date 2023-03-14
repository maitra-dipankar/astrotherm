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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <time.h>
#include <string.h>
#include <errno.h>

#include "thermapp.h"

#define ROUND_UP_512(num) (((num)+511)&~511)

ThermApp *
thermapp_open(void)
{
	ThermApp *thermapp = calloc(1, sizeof *thermapp);
	if (!thermapp) {
		perror("calloc");
		goto err1;
	}

	thermapp->cfg = calloc(1, sizeof *thermapp->cfg);
	if (!thermapp->cfg) {
		perror("calloc");
		goto err2;
	}

	thermapp->data_in = malloc(ROUND_UP_512(sizeof *thermapp->data_in));
	if (!thermapp->data_in) {
		perror("malloc");
		goto err2;
	}

	thermapp->data_done = malloc(ROUND_UP_512(sizeof *thermapp->data_done));
	if (!thermapp->data_done) {
		perror("malloc");
		goto err2;
	}

	//Initialize data struct
	// this init data was received from usbmonitor
	thermapp->cfg->preamble[0] = 0xa5a5;
	thermapp->cfg->preamble[1] = 0xa5a5;
	thermapp->cfg->preamble[2] = 0xa5a5;
	thermapp->cfg->preamble[3] = 0xa5d5;
	thermapp->cfg->modes = 0x0002; //test pattern low
	thermapp->cfg->data_09 = FRAME_HEIGHT;
	thermapp->cfg->data_0a = FRAME_WIDTH;
	thermapp->cfg->data_0b = FRAME_HEIGHT;
	thermapp->cfg->data_0c = FRAME_WIDTH;
	thermapp->cfg->data_0d = 0x0019;
	thermapp->cfg->data_0e = 0x0000;
	thermapp->cfg->VoutA = 0x075c;
	thermapp->cfg->data_11 = 0x0b85;
	thermapp->cfg->VoutC = 0x05f4;
	thermapp->cfg->VoutD = 0x0800;
	thermapp->cfg->VoutE = 0x0b85;
	thermapp->cfg->data_15 = 0x0b85;
	thermapp->cfg->data_16 = 0x0000;
	thermapp->cfg->data_17 = 0x0570;
	thermapp->cfg->data_18 = 0x0b85;
	thermapp->cfg->data_19 = 0x0040;
	thermapp->cfg->data_1b = 0x0000;
	thermapp->cfg->data_1c = 0x0050;
	thermapp->cfg->data_1d = 0x0003;
	thermapp->cfg->data_1e = 0x0000;
	thermapp->cfg->data_1f = 0x0fff;

	return thermapp;

err2:
	thermapp_close(thermapp);
err1:
	return NULL;
}

int
thermapp_usb_connect(ThermApp *thermapp)
{
	int ret;

	ret = libusb_init(&thermapp->ctx);
	if (ret) {
		fprintf(stderr, "libusb_init: %s\n", libusb_strerror(ret));
		return -1;
	}

	///FIXME: For Debug use libusb_open_device_with_vid_pid
	/// need to add search device
	thermapp->dev = libusb_open_device_with_vid_pid(thermapp->ctx, VENDOR, PRODUCT);
	if (!thermapp->dev) {
		ret = LIBUSB_ERROR_NO_DEVICE;
		fprintf(stderr, "libusb_open_device_with_vid_pid: %s\n", libusb_strerror(ret));
		return -1;
	}

	ret = libusb_set_configuration(thermapp->dev, 1);
	if (ret) {
		fprintf(stderr, "libusb_set_configuration: %s\n", libusb_strerror(ret));
		return -1;
	}

	//if (libusb_kernel_driver_active(thermapp->dev, 0))
	//	libusb_detach_kernel_driver(thermapp->dev, 0);

	ret = libusb_claim_interface(thermapp->dev, 0);
	if (ret) {
		fprintf(stderr, "libusb_claim_interface: %s\n", libusb_strerror(ret));
		return -1;
	}

	return 0;
}

static void
thermapp_cancel_async(ThermApp *thermapp, int internal)
{
	if (thermapp->transfer_in) {
		int ret = libusb_cancel_transfer(thermapp->transfer_in);
		if (ret && ret != LIBUSB_ERROR_NOT_FOUND) {
			fprintf(stderr, "libusb_cancel_transfer: %s\n", libusb_strerror(ret));
		}
	}

	if (thermapp->transfer_out) {
		int ret = libusb_cancel_transfer(thermapp->transfer_out);
		if (ret && ret != LIBUSB_ERROR_NOT_FOUND) {
			fprintf(stderr, "libusb_cancel_transfer: %s\n", libusb_strerror(ret));
		}
	}

	if (internal) {
		if (!thermapp->transfer_in && !thermapp->transfer_out) {
			// All transfers cancelled.
			// End the event loop and wake all waiters so they can exit.
			pthread_mutex_lock(&thermapp->mutex_getimage);
			thermapp->complete = 1;
			pthread_cond_broadcast(&thermapp->cond_getimage);
			pthread_mutex_unlock(&thermapp->mutex_getimage);
		}
	}
}

static void LIBUSB_CALL
transfer_cb_out(struct libusb_transfer *transfer)
{
	ThermApp *thermapp = (ThermApp *)transfer->user_data;

	if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		int ret = libusb_submit_transfer(transfer);
		if (ret) {
			fprintf(stderr, "libusb_submit_transfer: %s\n", libusb_strerror(ret));
		}
	} else if (transfer->status == LIBUSB_TRANSFER_ERROR
	        || transfer->status == LIBUSB_TRANSFER_NO_DEVICE
	        || transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		libusb_free_transfer(thermapp->transfer_out);
		thermapp->transfer_out = NULL;

		thermapp_cancel_async(thermapp, 1);
	}
}

static void LIBUSB_CALL
transfer_cb_in(struct libusb_transfer *transfer)
{
	ThermApp *thermapp = (ThermApp *)transfer->user_data;

	if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		// Device apparently only works with 512-byte chunks of data.
		// Note the packet is padded to a multiple of 512 bytes.
		if (transfer->actual_length % 512) {
			fprintf(stderr, "discarding partial transfer of size %u\n", transfer->actual_length);
			transfer->buffer = (unsigned char *)thermapp->data_in;
			transfer->length = TRANSFER_SIZE;
		} else if (transfer->actual_length) {
			unsigned char *buf = (unsigned char *)thermapp->data_in;
			size_t old = (unsigned char *)transfer->buffer - buf;
			size_t len = old + transfer->actual_length;

			if (!old) {
				// Sync to start of packet.
				// Look for preamble at start of 512-byte chunk.
				while (len >= 512) {
					if (memcmp(buf, thermapp->cfg, sizeof thermapp->cfg->preamble) == 0) {
						break;
					}
					buf += 512;
					len -= 512;
				}
				memmove(thermapp->data_in, buf, len);
			}

			if (len == ROUND_UP_512(sizeof *thermapp->data_in)) {
				// Frame complete.
				pthread_mutex_lock(&thermapp->mutex_getimage);
				struct thermapp_packet *tmp = thermapp->data_done;
				thermapp->data_done = thermapp->data_in;
				thermapp->data_in = tmp;
				pthread_cond_broadcast(&thermapp->cond_getimage);
				pthread_mutex_unlock(&thermapp->mutex_getimage);

				transfer->buffer = (unsigned char *)thermapp->data_in;
				len = 0;
			}

			transfer->buffer = (unsigned char *)thermapp->data_in + len;
			transfer->length = TRANSFER_SIZE;
			if (transfer->length > ROUND_UP_512(sizeof *thermapp->data_in) - len) {
				transfer->length = ROUND_UP_512(sizeof *thermapp->data_in) - len;
			}
		}

		int ret = libusb_submit_transfer(transfer);
		if (ret) {
			fprintf(stderr, "libusb_submit_transfer: %s\n", libusb_strerror(ret));
		}
	} else if (transfer->status == LIBUSB_TRANSFER_ERROR
	        || transfer->status == LIBUSB_TRANSFER_NO_DEVICE
	        || transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		libusb_free_transfer(thermapp->transfer_in);
		thermapp->transfer_in = NULL;

		thermapp_cancel_async(thermapp, 1);
	}
}

static void *
thermapp_read_async(void *ctx)
{
	ThermApp *thermapp = (ThermApp *)ctx;
	int ret;

	thermapp->transfer_out = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer(thermapp->transfer_out,
	                          thermapp->dev,
	                          LIBUSB_ENDPOINT_OUT | 2,
	                          (unsigned char *)thermapp->cfg,
	                          sizeof *thermapp->cfg,
	                          transfer_cb_out,
	                          thermapp,
	                          0);
	ret = libusb_submit_transfer(thermapp->transfer_out);
	if (ret) {
		fprintf(stderr, "libusb_submit_transfer: %s\n", libusb_strerror(ret));
		libusb_free_transfer(thermapp->transfer_out);
		thermapp->transfer_out = NULL;
	}

	thermapp->transfer_in = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer(thermapp->transfer_in,
	                          thermapp->dev,
	                          LIBUSB_ENDPOINT_IN | 1,
	                          (unsigned char *)thermapp->data_in,
	                          TRANSFER_SIZE,
	                          transfer_cb_in,
	                          (void *)thermapp,
	                          0);
	ret = libusb_submit_transfer(thermapp->transfer_in);
	if (ret) {
		fprintf(stderr, "libusb_submit_transfer: %s\n", libusb_strerror(ret));
		libusb_free_transfer(thermapp->transfer_in);
		thermapp->transfer_in = NULL;
	}

	while (thermapp->transfer_out || thermapp->transfer_in) {
		ret = libusb_handle_events_completed(thermapp->ctx, &thermapp->complete);
		if (ret) {
			fprintf(stderr, "libusb_handle_events_completed: %s\n", libusb_strerror(ret));
			if (ret == LIBUSB_ERROR_INTERRUPTED) /* stray signal */ {
				continue;
			} else {
				break;
			}
		}
	}

	return NULL;
}

// Create read and write thread
int
thermapp_thread_create(ThermApp *thermapp)
{
	int ret;

	thermapp->cond_getimage = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
	thermapp->mutex_getimage = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	thermapp->complete = 0;

	ret = pthread_create(&thermapp->pthread_read_async, NULL, thermapp_read_async, (void *)thermapp);
	if (ret) {
		fprintf(stderr, "pthread_create: %s\n", strerror(ret));
		return -1;
	}
	thermapp->started_read_async = 1;

	return 0;
}

int
thermapp_close(ThermApp *thermapp)
{
	if (!thermapp)
		return -1;

	thermapp_cancel_async(thermapp, 0);

	if (thermapp->started_read_async) {
		pthread_join(thermapp->pthread_read_async, NULL);
	}

	if (thermapp->dev) {
		libusb_release_interface(thermapp->dev, 0);
		libusb_close(thermapp->dev);
	}

	if (thermapp->ctx) {
		libusb_exit(thermapp->ctx);
	}

	free(thermapp->data_done);
	free(thermapp->data_in);
	free(thermapp->cfg);
	free(thermapp);

	return 0;
}

// This function for getting frame pixel data
int
thermapp_getImage(ThermApp *thermapp, int16_t *ImgData)
{
	int ret = 0;

	pthread_mutex_lock(&thermapp->mutex_getimage);
	pthread_cond_wait(&thermapp->cond_getimage, &thermapp->mutex_getimage);

	if (thermapp->complete) {
		ret = -1;
	} else {
		thermapp->serial_num = thermapp->data_done->header.serial_num_lo
		                     | thermapp->data_done->header.serial_num_hi << 16;
		thermapp->hardware_ver = thermapp->data_done->header.hardware_ver;
		thermapp->firmware_ver = thermapp->data_done->header.firmware_ver;
		thermapp->temperature = thermapp->data_done->header.temperature;
		thermapp->frame_count = thermapp->data_done->header.frame_count;

		memcpy(ImgData, thermapp->data_done->pixels_data, sizeof thermapp->data_done->pixels_data);
	}

	pthread_mutex_unlock(&thermapp->mutex_getimage);

	return ret;
}

uint32_t
thermapp_getSerialNumber(ThermApp *thermapp)
{
	return thermapp->serial_num;
}

uint16_t
thermapp_getHardwareVersion(ThermApp *thermapp)
{
	return thermapp->hardware_ver;
}

uint16_t
thermapp_getFirmwareVersion(ThermApp *thermapp)
{
	return thermapp->firmware_ver;
}

//We don't know offset and quant value for temperature.
//We use experimental value.
float
thermapp_getTemperature(ThermApp *thermapp)
{
	return (thermapp->temperature - 14336) * 0.00652;
}

uint16_t
thermapp_getFrameCount(ThermApp *thermapp)
{
	return thermapp->frame_count;
}
