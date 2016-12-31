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

#include <libdivecomputer/units.h>
#include <libdivecomputer/common.h>
#include <libdivecomputer/parser.h>

#include "extern.h"

typedef	struct dctool_ss_output_t {
	FILE *ostream;
	dctool_units_t units;
	unsigned int number;
} dctool_ss_output_t;

typedef struct sample_data_t {
	FILE *ostream;
	dctool_units_t units;
	unsigned int nsamples;
} sample_data_t;

static double
convert_depth(double value, dctool_units_t units)
{

	if (units == DCTOOL_UNITS_IMPERIAL)
		return value / FEET;
	else
		return value;
}

static double
convert_temperature(double value, dctool_units_t units)
{

	if (units == DCTOOL_UNITS_IMPERIAL)
		return value * (9.0 / 5.0) + 32.0;
	else
		return value;
}

static void
sample_cb(dc_sample_type_t type, dc_sample_value_t value, void *userdata)
{
	sample_data_t *sampledata = (sample_data_t *)userdata;

	if (DC_SAMPLE_EVENT == type)
		return;

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
			" depth=\"%.2f\"",
			convert_depth(value.depth, DCTOOL_UNITS_METRIC));
		break;
	case DC_SAMPLE_TEMPERATURE:
		fprintf(sampledata->ostream, 
			" temp=\"%.2f\"",
			convert_temperature(value.temperature, DCTOOL_UNITS_METRIC));
		break;
	default:
		break;
	}
}

dctool_output_t *
dctool_ss_output_new(dctool_units_t units)
{
	dctool_ss_output_t *output = NULL;

	output = malloc(sizeof(dctool_ss_output_t));

	if (output == NULL)
		goto error_exit;

	output->ostream = stdout;
	output->units = units;

	fputs("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
	      "<divelog program=\"divecmd\" version=\"0.0.1\">\n"
	      "<dives>\n", output->ostream);

	return((dctool_output_t *)output);

error_exit:
	return(NULL);
}

dc_status_t
dctool_ss_output_write(dctool_output_t *abstract, dc_parser_t *parser, 
	const unsigned char fingerprint[], unsigned int fsize)
{
	dctool_ss_output_t *output = (dctool_ss_output_t *)abstract;
	dc_status_t status = DC_STATUS_SUCCESS;
	sample_data_t	 sampledata;
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

	memset(&sampledata, 0, sizeof(sample_data_t));
	memset(&dt, 0, sizeof(dc_datetime_t));
	memset(&gasmix, 0, sizeof(dc_gasmix_t));
	memset(&tank, 0, sizeof(dc_tank_t));

	sampledata.ostream = output->ostream;
	sampledata.units = output->units;

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
		if (0 == divemode) 
			dm = "Freedive";
		else if (1 == divemode)
			dm = "Gauge";
		else if (2 == divemode)
			dm = "Opencircuit";
		else if (3 == divemode)
			dm = "Closedcircuit";
	}

	fprintf(output->ostream, 
		"<dive number=\"%u\" date=\"%04i-%02i-%02i\" "
		       "time=\"%02i:%02i:%02i\" duration=\"%02u:%02u\">\n", 
		       output->number,
		       dt.year, dt.month, dt.day,
		       dt.hour, dt.minute, dt.second,
		       divetime / 60, divetime % 60);

	if (fingerprint) {
		fprintf(output->ostream, "<fingerprint>");
		for (i = 0; i < fsize; ++i)
			fprintf (output->ostream, "%02X", fingerprint[i]);
		fprintf(output->ostream, "</fingerprint>\n");
	}

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

	fprintf(output->ostream, 
		"<depth max=\"%.2f %s\" mean=\"%.2f %s\" />\n", 
		convert_depth(maxdepth, output->units),
		DCTOOL_UNITS_IMPERIAL == output->units ?
		"ft" : "m",
		convert_depth(avgdepth, output->units),
		DCTOOL_UNITS_IMPERIAL == output->units ?
		"ft" : "m");

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
dctool_ss_output_free(dctool_output_t *abstract)
{
	dctool_ss_output_t *output = (dctool_ss_output_t *)abstract;

	if (NULL == output)
		return(DC_STATUS_SUCCESS);

	fputs("</dives>\n"
      	      "</divelog>\n", output->ostream);

	fclose(output->ostream);
	return(DC_STATUS_SUCCESS);
}
