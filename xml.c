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

static	const char *dcmd_types[] = {
	"time", /* DC_SAMPLE_TIME */
	"depth", /* DC_SAMPLE_DEPTH */
	"pressure", /* DC_SAMPLE_PRESSURE */
	"temperature", /* DC_SAMPLE_TEMPERATURE */
	"event", /* DC_SAMPLE_EVENT */
	"remaining-bottom-time", /* DC_SAMPLE_RBT */
	"heartbeat", /* DC_SAMPLE_HEARTBEAT */
	"bearing", /* DC_SAMPLE_BEARING */
	"vendor", /* DC_SAMPLE_VENDOR */
	"setpoint", /* DC_SAMPLE_SETPOINT */
	"ppo2", /* DC_SAMPLE_PPO2 */
	"cns", /* DC_SAMPLE_CNS */
	"deco", /* DC_SAMPLE_DECO */
	"gasmix", /* DC_SAMPLE_GASMIX */
};

/*
 * A DC_SAMPLE_TIME means that we're encountering a new sample.
 */
static void
sample_cb(dc_sample_type_t type, dc_sample_value_t value, void *userdata)
{
	struct dcmd_samp *sampledata = userdata;

	switch (type) {
	case DC_SAMPLE_TIME:
		if (sampledata->nsamples++)
			fputs(" />\n", sampledata->ostream);
		fprintf(sampledata->ostream, 
			"     <sample time=\"%u\" ", value.time);
		break;
	case DC_SAMPLE_DEPTH:
		fprintf(sampledata->ostream, 
			"depth=\"%.2f\" ", value.depth);
		break;
	case DC_SAMPLE_PRESSURE:
		fprintf(sampledata->ostream, 
			"pressure=\"%.2f\" tank=\"%u\" ", 
			value.pressure.value,
			value.pressure.tank + 1);
		break;
	case DC_SAMPLE_TEMPERATURE:
		fprintf(sampledata->ostream, 
			"temp=\"%.2f\" ", value.temperature);
		break;
	default:
		if ( ! verbose)
			break;
		fprintf(stderr, "Unhandled type: "
			"%s\n", dcmd_types[type]);
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
	      "<divelog program=\"divecmd\" version=\"" VERSION "\">\n"
	      " <dives>\n", p->ostream);

	return((struct dcmd_out *)p);
}

/*
 * Start with the prologue:
 * 
 * <dive number="NNN" [date="yyyy:mm:dd" time="hh:mm:ss"]
 *  [duration="mmm:ss"] [mode="xxxxxxx"]>
 */
static int
output_xml_write_dive(FILE *f, dc_parser_t *parser, size_t num)
{
	dc_status_t	 st;
	dc_datetime_t	 dt;
	unsigned int	 divetime = 0;
	dc_divemode_t 	 dm = DC_DIVEMODE_OC;

	memset(&dt, 0, sizeof(struct dc_datetime_t));

	fprintf(f, "  <dive number=\"%zu\" ", num);

	st = dc_parser_get_datetime(parser, &dt);
	if (st != DC_STATUS_SUCCESS && 
	    st != DC_STATUS_UNSUPPORTED) {
		warnx("error parsing the datetime");
		return(0);
	} else if (DC_STATUS_SUCCESS == st)
		fprintf(f, "date=\"%04i-%02i-%02i\" "
		        "time=\"%02i:%02i:%02i\" ",
			dt.year, dt.month, dt.day,
			dt.hour, dt.minute, dt.second);

	st = dc_parser_get_field
		(parser, DC_FIELD_DIVETIME, 0, &divetime);
	if (st != DC_STATUS_SUCCESS && 
	    st != DC_STATUS_UNSUPPORTED) {
		warnx("error parsing the divetime");
		return(0);
	} else if (DC_STATUS_SUCCESS == st)
		fprintf(f, "duration=\"%u\" ", divetime);

	st = dc_parser_get_field
		(parser, DC_FIELD_DIVEMODE, 0, &dm);
	if (st != DC_STATUS_SUCCESS && 
	    st != DC_STATUS_UNSUPPORTED) {
		warnx("error parsing the dive mode");
		return(0);
	} else if (DC_STATUS_SUCCESS == st) {
		if (DC_DIVEMODE_FREEDIVE == dm) 
			fprintf(f, "mode=\"freedive\" ");
		else if (DC_DIVEMODE_GAUGE == dm)
			fprintf(f, "mode=\"gauge\" ");
		else if (DC_DIVEMODE_OC == dm)
			fprintf(f, "mode=\"opencircuit\" ");
		else if (DC_DIVEMODE_CC == dm)
			fprintf(f, "mode=\"closedcircuit\" ");
	}

	fprintf(f, ">\n");
	return(1);
}

static int
output_xml_write_depth(FILE *f, dc_parser_t *parser)
{
	dc_status_t	 stmax, stavg;
	double		 max = 0.0, avg = 0.0;

	stmax = dc_parser_get_field
		(parser, DC_FIELD_MAXDEPTH, 0, &max);
	if (stmax != DC_STATUS_SUCCESS && 
	    stmax != DC_STATUS_UNSUPPORTED) {
		warnx ("error parsing the maxdepth");
		return(0);
	} 

	stavg = dc_parser_get_field
		(parser, DC_FIELD_AVGDEPTH, 0, &avg);
	if (stavg != DC_STATUS_SUCCESS && 
	    stavg != DC_STATUS_UNSUPPORTED) {
		warnx ("error parsing the avgdepth");
		return(0);
	}

	if (DC_STATUS_SUCCESS != stmax &&
	    DC_STATUS_SUCCESS != stavg)
		return(1);

	fprintf(f, "   <depth ");
	if (DC_STATUS_SUCCESS == stmax && max > 0.0)
		fprintf(f, "max=\"%.2f\" ", max);
	if (DC_STATUS_SUCCESS == stavg && avg > 0.0)
		fprintf(f, "mean=\"%.2f\" ", avg);
	fprintf(f, "/>\n");
	return(1);
}


/*
 * Write our [optional] tanks:
 *
 * <tanks>
 *   <tank num="NNN" [o2="NN%" n2="NN%" he="NN%"] />
 * </tanks>
 *
 * The mix number will later be referenced in gas switches.
 */
static int
output_xml_write_tanks(FILE *f, dc_parser_t *parser)
{
	unsigned int	 i, ntanks = 0;
	dc_status_t	 st;
	dc_tank_t 	 tank;

	memset(&tank, 0, sizeof(dc_tank_t));

	st = dc_parser_get_field
		(parser, DC_FIELD_TANK_COUNT, 0, &ntanks);
	if (st != DC_STATUS_SUCCESS && 
	    st != DC_STATUS_UNSUPPORTED) {
		warnx("error parsing the tank count");
		return(0);
	} else if (DC_STATUS_SUCCESS != st || 0 == ntanks)
		return(1);

	fprintf(f, "   <tanks>\n");

	for (i = 0; i < ntanks; ++i) {
		fprintf(f, "    <tank num=\"%u\" ", i + 1);
		st = dc_parser_get_field
			(parser, DC_FIELD_TANK, i, &tank);
		if (st != DC_STATUS_SUCCESS && 
		    st != DC_STATUS_UNSUPPORTED) {
			warnx("error parsing the tank");
			return(0);
		} else if (DC_STATUS_SUCCESS != st) {
			fprintf(f, ">\n");
			continue;
		}

		if (DC_GASMIX_UNKNOWN != tank.gasmix)
			fprintf(f, "gasmix=\"%u\" ", 
				tank.gasmix + 1);

		if (DC_TANKVOLUME_IMPERIAL == tank.type ||
		    (DC_TANKVOLUME_METRIC == tank.type &&
		     0 != tank.workpressure))
			fprintf(f, "volume=\"%g\" "
				"workpressure=\"%g\" ",
				tank.volume, tank.workpressure);
		else if (DC_TANKVOLUME_METRIC == tank.type)
			fprintf(f, "volume=\"%g\" ", tank.volume);

		fprintf(f, "beginpressure=\"%g\" "
			"endpressure=\"%g\" />\n",
			tank.beginpressure, tank.endpressure);
	}

	fprintf(f, "   </tanks>\n");
	return(1);
}

/*
 * Write our [optional] available gas mixtures:
 *
 * <gasmixes>
 *   <gasmix num="NNN" [o2="NN%" n2="NN%" he="NN%"] />
 * </gasmixes>
 *
 * The mix number will later be referenced in gas switches.
 */
static int
output_xml_write_gasses(FILE *f, dc_parser_t *parser)
{
	unsigned int	 i, ngases = 0;
	dc_status_t	 st;
	dc_gasmix_t 	 gm;

	memset(&gm, 0, sizeof(dc_gasmix_t));

	st = dc_parser_get_field
		(parser, DC_FIELD_GASMIX_COUNT, 0, &ngases);
	if (st != DC_STATUS_SUCCESS && 
	    st != DC_STATUS_UNSUPPORTED) {
		warnx("error parsing the gas mix count");
		return(0);
	} else if (DC_STATUS_SUCCESS != st || 0 == ngases)
		return(1);

	fprintf(f, "   <gasmixes>\n");

	for (i = 0; i < ngases; ++i) {
		fprintf(f, "    <gasmix num=\"%u\" ", i + 1);
		st = dc_parser_get_field
			(parser, DC_FIELD_GASMIX, i, &gm);
		if (st != DC_STATUS_SUCCESS && 
		    st != DC_STATUS_UNSUPPORTED) {
			warnx("error parsing the gas mix");
			return(0);
		} else if (DC_STATUS_SUCCESS != st) {
			fprintf(f, ">\n");
			continue;
		}

		fprintf(f, "o2=\"%.1f\" n2=\"%.1f\" " 
			"he=\"%.1f\" />\n", gm.oxygen * 100.0,
			gm.nitrogen * 100.0, gm.helium * 100.0);
	}

	fprintf(f, "   </gasmixes>\n");
	return(1);
}

dc_status_t
output_xml_write(struct dcmd_out *abstract, 
	size_t num, dc_parser_t *parser, const char *fpr)
{
	struct dcmd_xml *output = (struct dcmd_xml *)abstract;
	dc_status_t status = DC_STATUS_SUCCESS;
	struct dcmd_samp sampledata;
	int		 rc = 0;

	memset(&sampledata, 0, sizeof(struct dcmd_samp));

	sampledata.ostream = output->ostream;

	if ( ! output_xml_write_dive(output->ostream, parser, num))
		goto cleanup;

	fprintf(output->ostream, 
		"   <fingerprint>%s</fingerprint>\n", fpr);

	if ( ! output_xml_write_gasses(output->ostream, parser))
		goto cleanup;
	if ( ! output_xml_write_tanks(output->ostream, parser))
		goto cleanup;
	if ( ! output_xml_write_depth(output->ostream, parser))
		goto cleanup;

	fprintf(output->ostream, "   <samples>\n");

	status = dc_parser_samples_foreach
		(parser, sample_cb, &sampledata);
	if (status != DC_STATUS_SUCCESS) {
		warnx ("error parsing the sample data");
		goto cleanup;
	}

	if (sampledata.nsamples)
		fputs(" />\n", output->ostream);

	rc = 1;
cleanup:

	if (1 == rc)
		fputs("   </samples>\n"
		      "  </dive>\n", output->ostream);

	return(DC_STATUS_SUCCESS);
}

dc_status_t
output_xml_free(struct dcmd_out *abstract)
{
	struct dcmd_xml *output = (struct dcmd_xml *)abstract;

	if (NULL == output)
		return(DC_STATUS_SUCCESS);

	fputs(" </dives>\n"
      	      "</divelog>\n", output->ostream);

	fclose(output->ostream);
	return(DC_STATUS_SUCCESS);
}
