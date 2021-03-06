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
#include "config.h"

#include <assert.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

typedef struct dive_data_t {
	dc_descriptor_t	 *descriptor; /* device */
	dc_device_t	 *device; /* device */
	dc_buffer_t	**fingerprint; /* first fingerprint */
	dc_buffer_t	 *ofp; /* only this fingerprint */
	size_t	  	  number; /* dive number, from 1 */
	enum dcmd_type 	  type; /* type of output */
	struct dcmd_out	 *output; /* output handler */
	const struct dcmd_rng *range; /* range to display */
} dive_data_t;

/*
 * Check to see whether we fall within the range we care about.
 * If there is no range we care about, or if the dive doesn't support
 * extracting the time and date, return >0.
 * Else check that we fall (inclusively) within the range, returning
 * >0 we if do, 0 if we don't.
 * If errors occur, return <0.
 */
static int
check_range(dc_parser_t *parser, const struct dcmd_rng *rng)
{
	int	 	 rc;
	dc_ticks_t	 dtt;
	dc_datetime_t	 dt;

	if (NULL == rng)
		return(1);

	rc = dc_parser_get_datetime(parser, &dt);
	if (DC_STATUS_SUCCESS != rc) {
		if (DC_STATUS_UNSUPPORTED == rc)
			return(1);
		warnx("cannot extract datetime for range check");
		return(-1);
	}

	if ((dtt = dc_datetime_mktime(&dt)) < 0) {
		warn("cannot convert datetime for range check");
		return(-1);
	}

	if (dtt >= rng->start && dtt <= rng->end) {
		if (verbose)
			fprintf(stderr, "Dive: date range match\n");
		return(1);
	} 
	
	if (verbose)
		fprintf(stderr, "Dive: no date range match\n");
	return(0);
}

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
	 * If we were asked only to print one dive, then stop processing
	 * right now.
	 */

	if (NULL != dd->ofp) {
		if (dc_buffer_get_size(dd->ofp) == fprsz &&
		    0 == memcmp(dc_buffer_get_data(dd->ofp), fpr, fprsz)) {
			if (verbose)
				fprintf(stderr, "Dive: fingerprint match\n");
		} else {
			if (verbose)
				fprintf(stderr, "Dive: no fingerprint match\n");
			retc = 1;
			goto cleanup;
		}
	}

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

	rc = dc_parser_set_data(parser, data, size);
	if (rc != DC_STATUS_SUCCESS)
		goto cleanup;

	/* Check our date-time range, if applicable. */

	if (0 == (rc = check_range(parser, dd->range))) {
		retc = 1;
		goto cleanup;
	} else if (rc < 0)
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

	/* Exit on error or fingerprint match. */

	if (rc != DC_STATUS_SUCCESS ||
	    (NULL != dd->ofp &&
	     dc_buffer_get_size(dd->ofp) == fprsz &&
	     0 == memcmp(dc_buffer_get_data(dd->ofp), fpr, fprsz)))
		goto cleanup;

	retc = 1;
cleanup:
	dc_parser_destroy(parser);
	free(fpbuf);
	return(retc);
}

/*
 * Print information about an event.
 * We only do this when we're running in high-verbosity mode.
 */
static void
event_cb(dc_device_t *device, dc_event_type_t event, 
	const void *data, void *userdata)
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

static dc_status_t
parse(dc_context_t *context, dc_descriptor_t *descriptor, 
	const char *devname, struct dcmd_out *output,
	enum dcmd_type type, dc_buffer_t *fprint, 
	dc_buffer_t *ofprint, dc_buffer_t **lfprint,
	const struct dcmd_rng *rng)
{
	dc_status_t	 rc = DC_STATUS_SUCCESS;
	dc_device_t	*device = NULL;
	int		 events;
	dive_data_t	 dd;

	memset(&dd, 0, sizeof(dive_data_t));

	/* Open the device. */

	if (verbose)
		fprintf(stderr, "%s: opening: %s, %s\n", devname,
			dc_descriptor_get_vendor(descriptor),
			dc_descriptor_get_product(descriptor));

	rc = dc_device_open(&device, context, descriptor, devname);
	if (rc != DC_STATUS_SUCCESS) {
		warnx("%s: %s", devname, dctool_errmsg(rc));
		goto cleanup;
	}

	if (NULL != fprint && NULL == ofprint) {
		if (verbose)
			fprintf(stderr, "%s: setting "
				"fingerprint: %zu bytes\n", 
				devname,
				dc_buffer_get_size(fprint));
		rc = dc_device_set_fingerprint(device,
			dc_buffer_get_data(fprint),
			dc_buffer_get_size(fprint));
		if (DC_STATUS_SUCCESS != rc &&
	 	    DC_STATUS_UNSUPPORTED != rc)
			errx(EXIT_FAILURE, "%s: %s", 
				devname, dctool_errmsg(rc));
	}

	/* Register the event handler. */

	events = DC_EVENT_WAITING | DC_EVENT_PROGRESS | 
		 DC_EVENT_DEVINFO | DC_EVENT_CLOCK | 
		 DC_EVENT_VENDOR;

	if (verbose)
		fprintf(stderr, "%s: setting events\n", devname);
	rc = dc_device_set_events
		(device, events, event_cb, NULL);
	if (rc != DC_STATUS_SUCCESS) {
		warnx("%s: %s", devname, dctool_errmsg(rc));
		goto cleanup;
	}

	/* Register the cancellation handler. */

	rc = dc_device_set_cancel
		(device, dctool_cancel_cb, NULL);
	if (rc != DC_STATUS_SUCCESS) {
		warnx("%s: %s", devname, dctool_errmsg(rc));
		goto cleanup;
	}

	/* Initialize the dive data. */

	dd.descriptor = descriptor;
	dd.device = device;
	dd.fingerprint = lfprint;
	dd.output = output;
	dd.type = type;
	dd.ofp = ofprint;
	dd.range = rng;

	/* Download the dives. */

	rc = dc_device_foreach(device, dive_cb, &dd);
	if (rc != DC_STATUS_SUCCESS) {
		warnx("%s: %s", devname, dctool_errmsg(rc));
		goto cleanup;
	}

cleanup:
	dc_device_close(device);
	return(rc);
}

int
download(dc_context_t *context, dc_descriptor_t *descriptor, 
	const char *udev, enum dcmd_type type,
	dc_buffer_t *fprint, dc_buffer_t *ofprint, 
	dc_buffer_t **lfprint, const struct dcmd_rng *rng,
	const char *ident)
{
	int		 exitcode = 0;
	dc_status_t	 status = DC_STATUS_SUCCESS;
	struct dcmd_out	*output = NULL;

	/* Create the output. */

	switch (type) {
	case (DC_OUTPUT_XML):
		output = output_xml_new(descriptor, ident);
		break;
	default:
		output = output_list_new();
		break;
	}

	/* Parse the dives. */

	assert(NULL != output);
	status = parse(context, descriptor, udev, 
		output, type, fprint, ofprint, lfprint, rng);

	if (status != DC_STATUS_SUCCESS) {
		warnx("%s", dctool_errmsg(status));
		goto cleanup;
	}

	exitcode = 1;

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
