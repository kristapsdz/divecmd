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

struct	dcmd_list {
	FILE		*ostream;
	unsigned int	 number;
};

struct dcmd_out *
output_list_new(void)
{
	struct dcmd_list *p;

	if (NULL == (p = malloc(sizeof(struct dcmd_list))))
		err(EXIT_FAILURE, NULL);
		
	p->ostream = stdout;
	return((struct dcmd_out *)p);
}

dc_status_t
output_list_write(struct dcmd_out *abstract, dc_parser_t *parser, 
	const unsigned char fingerprint[], unsigned int fsize)
{
	struct dcmd_list *output = (struct dcmd_list *)abstract;
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_datetime_t	 dt;
	dc_divemode_t 	 divemode = DC_DIVEMODE_OC;
	double 	 	 maxdepth = 0.0;
	const char	*dm = NULL;
	int		 rc = 0;
	unsigned int	 divetime = 0;
	unsigned int	 i;

	memset(&dt, 0, sizeof(dc_datetime_t));

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

	rc = 1;

cleanup:
	return(DC_STATUS_SUCCESS);
}

dc_status_t
output_list_free(struct dcmd_out *abstract)
{
	struct dcmd_list *output = (struct dcmd_list *)abstract;

	if (NULL != output)
		fclose(output->ostream);

	return(DC_STATUS_SUCCESS);
}
