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
	FILE		*ostream; /* output file */
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
output_list_write(struct dcmd_out *arg, 
	size_t num, dc_parser_t *parser, const char *fpr)
{
	struct dcmd_list *output = (struct dcmd_list *)arg;
	dc_status_t 	  status = DC_STATUS_SUCCESS;
	dc_datetime_t	  dt;
	dc_divemode_t 	  divemode = DC_DIVEMODE_OC;
	const char	 *dm = NULL;

	memset(&dt, 0, sizeof(dc_datetime_t));

	/* Parse date and time of dive. */

	status = dc_parser_get_datetime(parser, &dt);
	if (status != DC_STATUS_SUCCESS && 
	    status != DC_STATUS_UNSUPPORTED) {
		warnx("error parsing datetime");
		return(DC_STATUS_SUCCESS);
	}

	/* Parse mode of dive. */

	status = dc_parser_get_field
		(parser, DC_FIELD_DIVEMODE, 0, &divemode);
	if (status != DC_STATUS_SUCCESS && 
	    status != DC_STATUS_UNSUPPORTED) {
		warnx("error parsing dive mode");
		return(DC_STATUS_SUCCESS);
	} else if (status != DC_STATUS_UNSUPPORTED) {
		if (0 == divemode) 
			dm = "free-dive";
		else if (1 == divemode)
			dm = "gauge";
		else if (2 == divemode)
			dm = "open-circuit";
		else if (3 == divemode)
			dm = "closed-circuit";
	}

	fprintf(output->ostream, 
		"%4zu. %04i-%02i-%02i, %02i:%02i:%02i (%s): %s\n",
		num, dt.year, dt.month, dt.day,
		dt.hour, dt.minute, dt.second, 
		NULL != dm ? dm : "unknown", fpr);

	return(DC_STATUS_SUCCESS);
}

dc_status_t
output_list_free(struct dcmd_out *arg)
{
	struct dcmd_list *output = (struct dcmd_list *)arg;

	if (NULL != output)
		fclose(output->ostream);

	return(DC_STATUS_SUCCESS);
}
