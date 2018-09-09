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
#include "config.h"

#if HAVE_ERR
# include <err.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "extern.h"

struct	dcmd_xml {
	FILE		*f; /* output stream */
};

struct	dcmd_samp {
	FILE		*f; /* output stream */
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

static	const char *dcmd_deco_types[] = {
	"ndl", /* DC_DECO_NDL */
	"safetystop", /* DC_DECO_SAFETYSTOP */
	"decostop", /* DC_DECO_DECOSTOP */
	"deepstop", /* DC_DECO_DEEPSTOP */
};

static	const char *dcmd_event_types[] = {
	"none", /* SAMPLE_EVENT_NONE */
	"decostop", /* SAMPLE_EVENT_DECOSTOP */
	"rbt", /* SAMPLE_EVENT_RBT */
	"ascent", /* SAMPLE_EVENT_ASCENT */
	"ceiling", /* SAMPLE_EVENT_CEILING */
	"workload", /* SAMPLE_EVENT_WORKLOAD */
	"transmitter", /* SAMPLE_EVENT_TRANSMITTER */
	"violation", /* SAMPLE_EVENT_VIOLATION */
	"bookmark", /* SAMPLE_EVENT_BOOKMARK */
	"surface", /* SAMPLE_EVENT_SURFACE */
	"safetystop", /* SAMPLE_EVENT_SAFETYSTOP */
	"gaschange", /* SAMPLE_EVENT_GASCHANGE */
	"safetystop_voluntary", /* SAMPLE_EVENT_SAFETYSTOP_VOLUNTARY */
	"safetystop_mandatory", /* SAMPLE_EVENT_SAFETYSTOP_MANDATORY */
	"deepstop", /* SAMPLE_EVENT_DEEPSTOP */
	"ceiling_safetystop", /* SAMPLE_EVENT_CEILING_SAFETYSTOP */
	"floor", /* SAMPLE_EVENT_FLOOR */
	"divetime", /* SAMPLE_EVENT_DIVETIME */
	"maxdepth", /* SAMPLE_EVENT_MAXDEPTH */
	"olf", /* SAMPLE_EVENT_OLF */
	"po2", /* SAMPLE_EVENT_PO2 */
	"airtime", /* SAMPLE_EVENT_AIRTIME */
	"rgbm", /* SAMPLE_EVENT_RGBM */
	"heading", /* SAMPLE_EVENT_HEADING */
	"tissuelevel", /* SAMPLE_EVENT_TISSUELEVEL */
	"gaschange2", /* SAMPLE_EVENT_GASCHANGE2 */
};

/*
 * A DC_SAMPLE_TIME means that we're encountering a new sample.
 */
static void
sample_cb(dc_sample_type_t type, dc_sample_value_t v, void *userdata)
{
	struct dcmd_samp *sd = userdata;
	unsigned int	  i, j;
	const unsigned char *cp;

	switch (type) {
	case DC_SAMPLE_TIME:
		if (sd->nsamples++)
			fputs("\t\t\t\t</sample>\n", sd->f);
		fprintf(sd->f, "\t\t\t\t"
			"<sample time=\"%u\">\n", v.time);
		break;
	case DC_SAMPLE_RBT:
		fprintf(sd->f, "\t\t\t\t\t"
			"<rbt value=\"%u\" />\n", v.rbt);
		break;
	case DC_SAMPLE_VENDOR:
		if (0 == v.vendor.size) {
			fprintf(sd->f, "\t\t\t\t\t"
				"<vendor type=\"%u\" />\n", 
				v.vendor.type);
			break;
		}
		fprintf(sd->f, "\t\t\t\t\t"
			"<vendor type=\"%u\">\n", v.vendor.type);
		cp = v.vendor.data;
		for (i = 0; i < v.vendor.size; ) {
			fprintf(sd->f, "\t\t\t\t\t\t");
			for (j = 0; i < v.vendor.size && j < 16; j++, i++)
				fprintf(sd->f, "%02x", cp[i]);
			fprintf(sd->f, "\n");
		}
		fprintf(sd->f, "\t\t\t\t\t"
			"</vendor>\n");
		break;
	case DC_SAMPLE_DEPTH:
		fprintf(sd->f, "\t\t\t\t\t"
			"<depth value=\"%.2f\" />\n", v.depth);
		break;
	case DC_SAMPLE_PRESSURE:
		fprintf(sd->f, "\t\t\t\t\t"
			"<pressure value=\"%.2f\" tank=\"%u\" />\n", 
			v.pressure.value,
			v.pressure.tank + 1);
		break;
	case DC_SAMPLE_TEMPERATURE:
		fprintf(sd->f, "\t\t\t\t\t"
			"<temp value=\"%.2f\" />\n", 
			v.temperature);
		break;
	case DC_SAMPLE_DECO:
		fprintf(sd->f, "\t\t\t\t\t"
			"<deco depth=\"%.2f\" type=\"%s\" duration=\"%u\" />\n", 
			v.deco.depth, dcmd_deco_types[v.deco.type],
			v.deco.time);
		break;
	case DC_SAMPLE_GASMIX:
		fprintf(sd->f, "\t\t\t\t\t"
			"<gaschange mix=\"%u\" />\n", v.gasmix);
		break;
	case DC_SAMPLE_CNS:
		fprintf(sd->f, "\t\t\t\t\t"
			"<cns value=\"%.2f\" />\n", v.cns);
		break;
	case DC_SAMPLE_EVENT:
		fprintf(sd->f, "\t\t\t\t\t<event type=\"%s\"",
			dcmd_event_types[v.event.type]);
		if (v.event.time)
			fprintf(sd->f, " duration=\"%u\"", v.event.time);
		if (v.event.flags)
			fprintf(sd->f, " flags=\"%u\"", v.event.flags);
		fprintf(sd->f, " />\n");
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
output_xml_new(dc_descriptor_t *descriptor, const char *ident)
{
	struct dcmd_xml *p = NULL;

	if (NULL == (p = malloc(sizeof(struct dcmd_xml))))
		err(EXIT_FAILURE, NULL);

	p->f = stdout;

	fprintf(p->f, 
		"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
		"<divelog program=\"divecmd\" version=\"" VERSION "\" "
		 "vendor=\"%s\" product=\"%s\" model=\"%u\"",
		 dc_descriptor_get_vendor(descriptor),
		 dc_descriptor_get_product(descriptor),
		 dc_descriptor_get_model(descriptor));
	if (NULL != ident && '\0' != *ident) 
		fprintf(p->f, " diver=\"%s\"", ident);
	fprintf(p->f, ">\n\t<dives>\n");

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

	fprintf(f, "\t\t<dive number=\"%zu\" ", num);

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
			fprintf(f, "mode=\"freedive\"");
		else if (DC_DIVEMODE_GAUGE == dm)
			fprintf(f, "mode=\"gauge\"");
		else if (DC_DIVEMODE_OC == dm)
			fprintf(f, "mode=\"opencircuit\"");
		else if (DC_DIVEMODE_CC == dm)
			fprintf(f, "mode=\"closedcircuit\"");
	}

	fputs(">\n", f);
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

	fputs("\t\t\t<depth ", f);
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
 *   <tank num="NNN" [gasmix="NNN"] ... />
 * </tanks>
 *
 * The mix number will later be referenced in pressure measurements.
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

	fputs("\t\t\t<tanks>\n", f);

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

	fputs("\t\t\t</tanks>\n", f);
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

	fputs("\t\t\t<gasmixes>\n", f);

	for (i = 0; i < ngases; ++i) {
		fprintf(f, "\t\t\t\t<gasmix num=\"%u\" ", i + 1);
		st = dc_parser_get_field
			(parser, DC_FIELD_GASMIX, i, &gm);
		if (st != DC_STATUS_SUCCESS && 
		    st != DC_STATUS_UNSUPPORTED) {
			warnx("error parsing the gas mix");
			return(0);
		} else if (DC_STATUS_SUCCESS != st) {
			fputs("/>\n", f);
			continue;
		}

		fprintf(f, "o2=\"%.1f\" n2=\"%.1f\" " 
			"he=\"%.1f\" />\n", gm.oxygen * 100.0,
			gm.nitrogen * 100.0, gm.helium * 100.0);
	}

	fputs("\t\t\t</gasmixes>\n", f);
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

	sampledata.f = output->f;

	if ( ! output_xml_write_dive(output->f, parser, num))
		goto cleanup;

	fprintf(output->f, "\t\t\t"
		"<fingerprint>%s</fingerprint>\n", fpr);

	if ( ! output_xml_write_gasses(output->f, parser))
		goto cleanup;
	if ( ! output_xml_write_tanks(output->f, parser))
		goto cleanup;
	if ( ! output_xml_write_depth(output->f, parser))
		goto cleanup;

	fputs("\t\t\t<samples>\n", output->f);

	status = dc_parser_samples_foreach
		(parser, sample_cb, &sampledata);
	if (status != DC_STATUS_SUCCESS) {
		warnx ("error parsing the sample data");
		goto cleanup;
	}

	if (sampledata.nsamples)
		fputs("\t\t\t\t</sample>\n", output->f);

	rc = 1;
cleanup:

	if (1 == rc)
		fputs("\t\t\t</samples>\n"
		      "\t\t</dive>\n", output->f);

	return(DC_STATUS_SUCCESS);
}

dc_status_t
output_xml_free(struct dcmd_out *abstract)
{
	struct dcmd_xml *output = (struct dcmd_xml *)abstract;

	if (NULL == output)
		return(DC_STATUS_SUCCESS);

	fputs("\t</dives>\n"
      	      "</divelog>\n", output->f);

	fclose(output->f);
	return(DC_STATUS_SUCCESS);
}
