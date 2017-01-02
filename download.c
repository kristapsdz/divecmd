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

#include <assert.h>
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
	const char	  *cachedir;
	dc_event_devinfo_t devinfo;
} event_data_t;

typedef struct dive_data_t {
	dc_device_t	 *device; /* device */
	dc_buffer_t	**fingerprint; /* first fingerprint */
	dc_buffer_t	 *ofp; /* only this fingerprint */
	size_t	  	  number; /* dive number, from 1 */
	enum dcmd_type 	  type; /* type of output */
	struct dcmd_out	 *output; /* output handler */
} dive_data_t;

static int
dive_cb(const unsigned char *data, unsigned int size, 
	const unsigned char *fpr, unsigned int fprsz, 
	void *userdata)
{
	dive_data_t	*dd = userdata;
	dc_status_t	 rc = DC_STATUS_SUCCESS;
	dc_parser_t	*parser = NULL;
	dc_buffer_t	*fp;
	unsigned int	 i;
	char		*fpbuf = NULL;
	int		 retc = 0;

	dd->number++;

	if (NULL == (fpbuf = malloc(fprsz * 2 + 1)))
		err(EXIT_FAILURE, NULL);

	for (i = 0; i < fprsz; i++)
		snprintf(&fpbuf[i * 2], 3, "%02X", fpr[i]);

	if (verbose)
		fprintf(stderr, "Dive: number=%zu, "
			"size=%u, fingerprint=%s\n", 
			dd->number, size, fpbuf);

	/* 
	 * Keep a copy of the most recent fingerprint. Because dives are
	 * guaranteed to be downloaded in reverse order, the most recent
	 * dive is always the first dive.
	 */

	if (dd->number == 1) {
		fp = dc_buffer_new(fprsz);
		dc_buffer_append(fp, fpr, fprsz);
		*dd->fingerprint = fp;
	}

	/* Create the parser. */

	rc = dc_parser_new(&parser, dd->device);
	if (rc != DC_STATUS_SUCCESS)
		goto cleanup;

	/* Register the data. */

	rc = dc_parser_set_data (parser, data, size);
	if (rc != DC_STATUS_SUCCESS)
		goto cleanup;

	/* Parse the dive data. */

	switch (dd->type) {
	case (DC_OUTPUT_XML):
		rc = output_xml_write(dd->output, 
			dd->number, parser, fpbuf);
		break;
	case (DC_OUTPUT_LIST):
		rc = output_list_write
			(dd->output, dd->number, parser, fpbuf);
		break;
	}

	if (rc != DC_STATUS_SUCCESS)
		goto cleanup;

	/*
	 * If we were asked only to print one dive, then stop processing
	 * right now.
	 */

	if (NULL != dd->ofp &&
	    dc_buffer_get_size(dd->ofp) == fprsz &&
	    0 == memcmp(dc_buffer_get_data(dd->ofp), fpr, fprsz)) {
		if (verbose)
			fprintf(stderr, "Dive: fingerprint match\n");
		goto cleanup;
	}

	retc = 1;
cleanup:
	dc_parser_destroy(parser);
	free(fpbuf);
	return(retc);
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
parse(dc_context_t *context, dc_descriptor_t *descriptor, 
	const char *devname, struct dcmd_out *output,
	enum dcmd_type type, dc_buffer_t *fprint, dc_buffer_t *ofprint)
{
	dc_status_t	 rc = DC_STATUS_SUCCESS;
	dc_device_t	*device = NULL;
	dc_buffer_t	*ofingerprint = NULL;
	int		 events;
	event_data_t	 eventdata;
	dive_data_t	 dd;

	memset(&eventdata, 0, sizeof(event_data_t));
	memset(&dd, 0, sizeof(dive_data_t));

	/* Open the device. */

	rc = dc_device_open(&device, context, descriptor, devname);
	if (rc != DC_STATUS_SUCCESS) {
		warnx("%s: %s", devname, dctool_errmsg(rc));
		goto cleanup;
	}

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	if (NULL != fprint && NULL == ofprint) {
		rc = dc_device_set_fingerprint(device,
			dc_buffer_get_data(fprint),
			dc_buffer_get_size(fprint));
		if (DC_STATUS_SUCCESS != rc &&
	 	    DC_STATUS_UNSUPPORTED != rc)
			err(EXIT_FAILURE, "%s", dctool_errmsg(rc));
	}

	/* Register the event handler. */

	events = DC_EVENT_WAITING | DC_EVENT_PROGRESS | 
		 DC_EVENT_DEVINFO | DC_EVENT_CLOCK | 
		 DC_EVENT_VENDOR;

	rc = dc_device_set_events(device, events, event_cb, &eventdata);
	if (rc != DC_STATUS_SUCCESS) {
		warnx("%s: %s", devname, dctool_errmsg(rc));
		goto cleanup;
	}

	/* Register the cancellation handler. */

	rc = dc_device_set_cancel(device, dctool_cancel_cb, NULL);
	if (rc != DC_STATUS_SUCCESS) {
		warnx("%s: %s", devname, dctool_errmsg(rc));
		goto cleanup;
	}

	/* Initialize the dive data. */

	dd.device = device;
	dd.fingerprint = &ofingerprint;
	dd.output = output;
	dd.type = type;
	dd.ofp = ofprint;

	/* Download the dives. */

	rc = dc_device_foreach(device, dive_cb, &dd);
	if (rc != DC_STATUS_SUCCESS) {
		warnx("%s: %s", devname, dctool_errmsg(rc));
		goto cleanup;
	}

cleanup:
	dc_buffer_free(ofingerprint);
	dc_device_close(device);
	return(rc);
}

int
download(dc_context_t *context, dc_descriptor_t *descriptor, 
	const char *udev, enum dcmd_type type,
	dc_buffer_t *fprint, dc_buffer_t *ofprint)
{
	int		 exitcode = EXIT_FAILURE;
	dc_status_t	 status = DC_STATUS_SUCCESS;
	struct dcmd_out	*output = NULL;

	/* Create the output. */

	switch (type) {
	case (DC_OUTPUT_XML):
		output = output_xml_new();
		break;
	default:
		output = output_list_new();
		break;
	}

	/* Parse the dives. */

	assert(NULL != output);
	status = parse(context, descriptor, 
		udev, output, type, fprint, ofprint);

	if (status != DC_STATUS_SUCCESS) {
		warnx("%s", dctool_errmsg(status));
		goto cleanup;
	}

	exitcode = EXIT_SUCCESS;
cleanup:
	switch (type) {
	case (DC_OUTPUT_XML):
		output_xml_free(output);
		break;
	default:
		output_list_free(output);
		break;
	}

	return(exitcode);
}
