/*	$Id$ */
/*
 * Copyright (C) 2016 Jef Driesen
 * Copyright (C) 2016 Kristaps Dzonsons
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
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <libdivecomputer/common.h>
#include <libdivecomputer/parser.h>

#include "extern.h"

struct	dcmd_xml {
	FILE		*ostream; /* output stream */
};

struct	dcmd_samp {
	FILE		*ostream; /* output stream */
	unsigned int	 nsamples; /* num samples parsed */
};

static	const char *dcmd_events[] = {
	"none", 
	"deco", 
	"rbt", 
	"ascent", 
	"ceiling", 
	"workload", 
	"transmitter", 
	"violation", 
	"bookmark", 
	"surface", 
	"safety stop", 
	"gaschange",
	"safety stop (voluntary)", 
	"safety stop (mandatory)", 
	"deepstop",
	"ceiling (safety stop)", 
	"floor", 
	"divetime", 
	"maxdepth",
	"OLF", 
	"PO2", 
	"airtime", 
	"rgbm", 
	"heading", 
	"tissue level warning",
	"gaschange2"
};

static void
sample_cb(dc_sample_type_t type, dc_sample_value_t value, void *userdata)
{
	struct dcmd_samp *sampledata = userdata;

	switch (type) {
	case DC_SAMPLE_TIME:
		if (sampledata->nsamples++)
			fputs(" />\n", sampledata->ostream);
		fprintf(sampledata->ostream, 
			"<sample time=\"%02u:%02u\"", 
			value.time / 60, value.time % 60);
		break;
	case DC_SAMPLE_DEPTH:
		fprintf(sampledata->ostream, 
			" depth=\"%.2f\"", value.depth);
		break;
	case DC_SAMPLE_TEMPERATURE:
		fprintf(sampledata->ostream, 
			" temp=\"%.2f\"", value.temperature);
		break;
	default:
		if ( ! verbose)
			break;
		fprintf(stderr, "Unhandled type: "
			"%s\n", dcmd_events[type]);
		break;
	}
}

struct dcmd_out *
output_xml_new(void)
{
	struct dcmd_xml *p = NULL;

	if (NULL == (p = malloc(sizeof(struct dcmd_xml))))
		err(EXIT_FAILURE, NULL);

	p->ostream = stdout;

	fputs("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
	      "<divelog program=\"divecmd\" version=\"0.0.1\">\n"
	      "<dives>\n", p->ostream);

	return((struct dcmd_out *)p);
}

dc_status_t
output_xml_write(struct dcmd_out *abstract, 
	size_t num, dc_parser_t *parser, const char *fpr)
{
	struct dcmd_xml *output = (struct dcmd_xml *)abstract;
	dc_status_t status = DC_STATUS_SUCCESS;
	struct dcmd_samp sampledata;
	dc_datetime_t	 dt;
	dc_gasmix_t 	 gasmix;
	dc_tank_t 	 tank;
	dc_divemode_t 	 divemode = DC_DIVEMODE_OC;
	double maxdepth = 0.0, avgdepth = 0.0;
	const char	*dm = NULL;
	int		 rc = 0;
	unsigned int	 divetime = 0;
	unsigned int	 ngases = 0;
	unsigned int	 ntanks = 0;
	unsigned int	 i;

	memset(&sampledata, 0, sizeof(struct dcmd_samp));
	memset(&dt, 0, sizeof(dc_datetime_t));
	memset(&gasmix, 0, sizeof(dc_gasmix_t));
	memset(&tank, 0, sizeof(dc_tank_t));

	sampledata.ostream = output->ostream;

	/* Parse date and time of dive. */

	status = dc_parser_get_datetime (parser, &dt);
	if (status != DC_STATUS_SUCCESS && 
	    status != DC_STATUS_UNSUPPORTED) {
		warnx("Error parsing the datetime.");
		goto cleanup;
	}

	/* Parse time of dive. */

	status = dc_parser_get_field(parser, DC_FIELD_DIVETIME, 0, &divetime);
	if (status != DC_STATUS_SUCCESS && 
	    status != DC_STATUS_UNSUPPORTED) {
		warnx("Error parsing the divetime.");
		goto cleanup;
	}

	/* Parse maximum depth of dive. */

	status = dc_parser_get_field(parser, DC_FIELD_MAXDEPTH, 0, &maxdepth);
	if (status != DC_STATUS_SUCCESS && 
	    status != DC_STATUS_UNSUPPORTED) {
		warnx ("Error parsing the maxdepth.");
		goto cleanup;
	}

	status = dc_parser_get_field(parser, DC_FIELD_AVGDEPTH, 0, &avgdepth);
	if (status != DC_STATUS_SUCCESS && 
	    status != DC_STATUS_UNSUPPORTED) {
		warnx ("Error parsing the avgdepth.");
		goto cleanup;
	}

	status = dc_parser_get_field(parser, DC_FIELD_DIVEMODE, 0, &divemode);
	if (status != DC_STATUS_SUCCESS && 
	    status != DC_STATUS_UNSUPPORTED) {
		warnx("Error parsing the dive mode.");
		goto cleanup;
	}

	if (status != DC_STATUS_UNSUPPORTED) {
		if (DC_DIVEMODE_FREEDIVE == divemode) 
			dm = "freedive";
		else if (DC_DIVEMODE_GAUGE == divemode)
			dm = "gauge";
		else if (DC_DIVEMODE_OC == divemode)
			dm = "opencircuit";
		else if (DC_DIVEMODE_CC == divemode)
			dm = "closedcircuit";
	}

	fprintf(output->ostream, 
		"<dive number=\"%zu\" date=\"%04i-%02i-%02i\" "
		       "time=\"%02i:%02i:%02i\" duration=\"%02u:%02u\">\n", 
		       num, dt.year, dt.month, dt.day,
		       dt.hour, dt.minute, dt.second,
		       divetime / 60, divetime % 60);

	fprintf(output->ostream, "<fingerprint>%s</fingerprint>\n", fpr);

	status = dc_parser_get_field(parser, DC_FIELD_GASMIX_COUNT, 0, &ngases);

	if (status != DC_STATUS_SUCCESS && 
	    status != DC_STATUS_UNSUPPORTED) {
		warnx ("Error parsing the gas mix count.");
		goto cleanup;
	}

	for (i = 0; i < ngases; ++i) {
		status = dc_parser_get_field(parser, DC_FIELD_GASMIX, i, &gasmix);

		if (status != DC_STATUS_SUCCESS && 
		    status != DC_STATUS_UNSUPPORTED) {
			warnx ("Error parsing the gas mix.");
			goto cleanup;
		}

		if (gasmix.oxygen > 0.2 &&
		    gasmix.oxygen < 0.22) {
			fputs("<cylinder />\n", output->ostream);
			continue;
		}

		fprintf(output->ostream,
			"<cylinder o2=\"%.1f\" />\n",
			gasmix.oxygen * 100.0);
	}

	fprintf(output->ostream,
		"<divecomputer dctype=\"%s\">\n", 
		NULL == dm ? "unknown" : dm);

	/* FIXME: remove "m". */

	fprintf(output->ostream, 
		"<depth max=\"%.2f m\" mean=\"%.2f m\" />\n", 
		maxdepth, avgdepth);

	status = dc_parser_get_field(parser, DC_FIELD_TANK_COUNT, 0, &ntanks);
	if (status != DC_STATUS_SUCCESS && 
	    status != DC_STATUS_UNSUPPORTED) {
		warnx ("Error parsing the tank count.");
		goto cleanup;
	}

	status = dc_parser_samples_foreach(parser, sample_cb, &sampledata);
	if (status != DC_STATUS_SUCCESS) {
		warnx ("Error parsing the sample data.");
		goto cleanup;
	}

	if (sampledata.nsamples)
		fputs(" />\n", output->ostream);

	rc = 1;
cleanup:

	if (1 == rc)
		fputs("</divecomputer>\n"
		      "</dive>\n", output->ostream);

	return(DC_STATUS_SUCCESS);
}

dc_status_t
output_xml_free(struct dcmd_out *abstract)
{
	struct dcmd_xml *output = (struct dcmd_xml *)abstract;

	if (NULL == output)
		return(DC_STATUS_SUCCESS);

	fputs("</dives>\n"
      	      "</divelog>\n", output->ostream);

	fclose(output->ostream);
	return(DC_STATUS_SUCCESS);
}
