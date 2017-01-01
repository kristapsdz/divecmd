/*	$Id$ */
/*
 * Copyright (C) 2015, 2016 Jef Driesen
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
#ifndef EXTERN_H
#define EXTERN_H

#include <libdivecomputer/context.h>
#include <libdivecomputer/descriptor.h>
#include <libdivecomputer/device.h>
#include <libdivecomputer/common.h>
#include <libdivecomputer/parser.h>

typedef struct dctool_output_t dctool_output_t;

typedef enum dctool_units_t {
	DCTOOL_UNITS_METRIC,
	DCTOOL_UNITS_IMPERIAL
} dctool_units_t;

enum	dcmd_out {
	DC_OUTPUT_FULL,
	DC_OUTPUT_LIST
};

__BEGIN_DECLS

const char	*dctool_errmsg(dc_status_t);
void		 dctool_event_cb(dc_device_t *, 
			dc_event_type_t, const void *, void *);
int		 download(dc_context_t *, dc_descriptor_t *, 
			const char *, enum dcmd_out);

dc_status_t	 output_list_free(dctool_output_t *);
dctool_output_t *output_list_new(void);
dc_status_t	 output_list_write(dctool_output_t *, dc_parser_t *, 
			const unsigned char[], unsigned int);

dc_status_t	 output_full_free(dctool_output_t *);
dctool_output_t *output_full_new(dctool_units_t);
dc_status_t	 output_full_write(dctool_output_t *, dc_parser_t *, 
			const unsigned char[], unsigned int);

int		 dctool_cancel_cb(void *userdata);

extern int	 verbose;

__END_DECLS

#endif /* !EXTERN_H */
