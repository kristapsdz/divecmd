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
#ifdef __OpenBSD__
# include <sys/param.h>
#endif

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <libdivecomputer/context.h>
#include <libdivecomputer/descriptor.h>
#include <libdivecomputer/iterator.h>

#include "extern.h"

int
dctool_list_run(dc_context_t *context)
{
	dc_iterator_t	*iter = NULL;
	dc_descriptor_t *desc = NULL;
	dc_status_t	 st;
	const char	*vendor, *product;

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	(void)context;

	if (DC_STATUS_SUCCESS != 
	    (st = dc_descriptor_iterator(&iter))) {
		warnx("dc_descriptor_iterator: %s", dctool_errmsg(st));
		return(EXIT_FAILURE);
	}

	while (DC_STATUS_SUCCESS == 
	       (st = dc_iterator_next(iter, &desc))) {
		vendor = dc_descriptor_get_vendor(desc);
		product = dc_descriptor_get_product(desc);
		printf("%s %s\n", 
			NULL == vendor ? "(none)" : vendor, 
			NULL == product ? "(none)" : product);
		dc_descriptor_free(desc);
	}

	/* We don't care about errors now. */

	if (DC_STATUS_DONE != st)
		warnx("dc_iterator_next: %s", dctool_errmsg(st));

	if (DC_STATUS_SUCCESS != (st = dc_iterator_free(iter)))
		warnx("dc_iterator_free: %s", dctool_errmsg(st));

	return(EXIT_SUCCESS);
}
