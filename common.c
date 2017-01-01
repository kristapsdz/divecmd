/*	$Id$ */
/*
 * Copyright (C) 2015 Jef Driesen
 * Copyright (C) 2016 Kristaps Dzonsons, kristaps@bsd.lv
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */

#include <string.h>
#include <stdio.h>

#include "extern.h"

const char *
dctool_errmsg(dc_status_t status)
{

	switch (status) {
	case DC_STATUS_SUCCESS:
		return "success";
	case DC_STATUS_UNSUPPORTED:
		return "unsupported operation";
	case DC_STATUS_INVALIDARGS:
		return "invalid arguments";
	case DC_STATUS_NOMEMORY:
		return "out of memory";
	case DC_STATUS_NODEVICE:
		return "no device found";
	case DC_STATUS_NOACCESS:
		return "access denied";
	case DC_STATUS_IO:
		return "input/output error";
	case DC_STATUS_TIMEOUT:
		return "timeout";
	case DC_STATUS_PROTOCOL:
		return "protocol error";
	case DC_STATUS_DATAFORMAT:
		return "data format error";
	case DC_STATUS_CANCELLED:
		return "cancelled";
	default:
		return "unknown error";
	}
}

/*
 * Print information about an event.
 * We only do this when we're running in high-verbosity mode.
 */
void
dctool_event_cb(dc_device_t *device, 
	dc_event_type_t event, const void *data, void *userdata)
{
	const dc_event_progress_t *progress;
	const dc_event_devinfo_t  *devinfo;
	const dc_event_clock_t    *clock;
	const dc_event_vendor_t   *vendor;
	unsigned int	 	   i;

	(void)device;
	(void)userdata;

	if (verbose < 2)
		return;

	switch (event) {
	case DC_EVENT_WAITING:
		fprintf(stderr, "Event: waiting for user action\n");
		break;
	case DC_EVENT_PROGRESS:
		progress = data;
		fprintf(stderr, "Event: progress %3.2f%% (%u/%u)\n",
			100.0 * (double) progress->current / 
			(double) progress->maximum,
			progress->current, progress->maximum);
		break;
	case DC_EVENT_DEVINFO:
		devinfo = data;
		fprintf(stderr, "Event: model=%u (0x%08x), "
			"firmware=%u (0x%08x), serial=%u (0x%08x)\n",
			devinfo->model, devinfo->model,
			devinfo->firmware, devinfo->firmware,
			devinfo->serial, devinfo->serial);
		break;
	case DC_EVENT_CLOCK:
		clock = data;
		fprintf(stderr, "Event: systime=%lld, devtime=%u\n",
			(long long)clock->systime, clock->devtime);
		break;
	case DC_EVENT_VENDOR:
		vendor = data;
		fprintf(stderr, "Event: vendor=");
		for (i = 0; i < vendor->size; ++i)
			fprintf(stderr, "%02X", vendor->data[i]);
		fputc('\n', stderr);
		break;
	default:
		break;
	}
}


static unsigned char
hex2dec (unsigned char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	else if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;
	else if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	else
		return 0;
}

dc_buffer_t *
dctool_convert_hex2bin (const char *str)
{
	// Get the length of the fingerprint data.
	size_t nbytes = (str ? strlen (str) / 2 : 0);
	if (nbytes == 0)
		return NULL;

	// Allocate a memory buffer.
	dc_buffer_t *buffer = dc_buffer_new (nbytes);

	// Convert the hexadecimal string.
	for (unsigned int i = 0; i < nbytes; ++i) {
		unsigned char msn = hex2dec (str[i * 2 + 0]);
		unsigned char lsn = hex2dec (str[i * 2 + 1]);
		unsigned char byte = (msn << 4) + lsn;

		dc_buffer_append (buffer, &byte, 1);
	}

	return buffer;
}
