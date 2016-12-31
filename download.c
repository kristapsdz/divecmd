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
#ifdef __OpenBSD_
# include <sys/param.h>
#endif

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libdivecomputer/context.h>
#include <libdivecomputer/descriptor.h>
#include <libdivecomputer/device.h>
#include <libdivecomputer/parser.h>

#include "extern.h"

typedef struct event_data_t {
	const char *cachedir;
	dc_event_devinfo_t devinfo;
} event_data_t;

typedef struct dive_data_t {
	dc_device_t *device;
	dc_buffer_t **fingerprint;
	unsigned int number;
	dctool_output_t *output;
} dive_data_t;

static int
dive_cb(const unsigned char *data, unsigned int size, 
	const unsigned char *fingerprint, 
	unsigned int fsize, void *userdata)
{
	dive_data_t	*divedata = userdata;
	dc_status_t	 rc = DC_STATUS_SUCCESS;
	dc_parser_t	*parser = NULL;
	dc_buffer_t	*fp;
	unsigned int	 i;

	divedata->number++;

	if (verbose) {
		fprintf(stderr, "Dive: number=%u, size=%u, fingerprint=", 
			divedata->number, size);
		for (i = 0; i < fsize; ++i)
			fprintf(stderr, "%02X", fingerprint[i]);
		fputc('\n', stderr);
	}

	/* 
	 * Keep a copy of the most recent fingerprint. Because dives are
	 * guaranteed to be downloaded in reverse order, the most recent
	 * dive is always the first dive.
	 */

	if (divedata->number == 1) {
		fp = dc_buffer_new(fsize);
		dc_buffer_append(fp, fingerprint, fsize);
		*divedata->fingerprint = fp;
	}

	/* Create the parser. */

	rc = dc_parser_new(&parser, divedata->device);
	if (rc != DC_STATUS_SUCCESS)
		goto cleanup;

	/* Register the data. */

	rc = dc_parser_set_data (parser, data, size);
	if (rc != DC_STATUS_SUCCESS)
		goto cleanup;

	/* Parse the dive data. */

	rc = dctool_ss_output_write(divedata->output, 
		parser, fingerprint, fsize);
	if (rc != DC_STATUS_SUCCESS)
		goto cleanup;

cleanup:
	dc_parser_destroy(parser);
	return(1);
}

static void
event_cb(dc_device_t *device, dc_event_type_t event, 
	const void *data, void *userdata)
{
	const dc_event_devinfo_t *devinfo = (const dc_event_devinfo_t *)data;
	event_data_t	*eventdata = (event_data_t *)userdata;

	/* Forward to the default event handler. */

	dctool_event_cb(device, event, data, userdata);

	switch(event) {
	case DC_EVENT_DEVINFO:
		/* 
		 * Keep a copy of the event data. It will be used for
		 * generating the fingerprint filename again after a
		 * (successful) download.
		 */
		eventdata->devinfo = *devinfo;
		break;
	default:
		break;
	}
}

static dc_status_t
download(dc_context_t *context, dc_descriptor_t *descriptor, 
	const char *devname, dctool_output_t *output)
{
	dc_status_t	 rc = DC_STATUS_SUCCESS;
	dc_device_t	*device = NULL;
	dc_buffer_t	*ofingerprint = NULL;
	int		 events;
	event_data_t	 eventdata;
	dive_data_t	 divedata;

	memset(&eventdata, 0, sizeof(event_data_t));
	memset(&divedata, 0, sizeof(dive_data_t));

	/* Open the device. */

	rc = dc_device_open(&device, context, descriptor, devname);
	if (rc != DC_STATUS_SUCCESS)
		goto cleanup;

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	/* Register the event handler. */

	events = DC_EVENT_WAITING | DC_EVENT_PROGRESS | 
		 DC_EVENT_DEVINFO | DC_EVENT_CLOCK | DC_EVENT_VENDOR;

	rc = dc_device_set_events(device, events, event_cb, &eventdata);

	if (rc != DC_STATUS_SUCCESS)
		goto cleanup;

	/* Register the cancellation handler. */

	rc = dc_device_set_cancel(device, dctool_cancel_cb, NULL);
	if (rc != DC_STATUS_SUCCESS)
		goto cleanup;

	/* Initialize the dive data. */

	divedata.device = device;
	divedata.fingerprint = &ofingerprint;
	divedata.number = 0;
	divedata.output = output;

	/* Download the dives. */

	rc = dc_device_foreach(device, dive_cb, &divedata);
	if (rc != DC_STATUS_SUCCESS)
		goto cleanup;

cleanup:
	dc_buffer_free(ofingerprint);
	dc_device_close(device);
	return(rc);
}

int
dctool_download_run(dc_context_t *context, 
	dc_descriptor_t *descriptor, const char *udev)
{
	int		 exitcode = EXIT_FAILURE;
	dc_status_t	 status = DC_STATUS_SUCCESS;
	dctool_output_t	*output = NULL;
	dctool_units_t	 units = DCTOOL_UNITS_METRIC;

	/* Create the output. */

	if (NULL == (output = dctool_ss_output_new(units)))
		goto cleanup;

	/* Download the dives. */

	status = download(context, descriptor, udev, output);

	if (status != DC_STATUS_SUCCESS) {
		warnx("%s", dctool_errmsg(status));
		goto cleanup;
	}

	exitcode = EXIT_SUCCESS;

cleanup:
	dctool_ss_output_free(output);
	return(exitcode);
}
