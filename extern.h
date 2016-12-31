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

__BEGIN_DECLS

const char	*dctool_errmsg(dc_status_t status);
void		 dctool_event_cb(dc_device_t *device, dc_event_type_t event, const void *data, void *userdata);
dc_buffer_t	*dctool_convert_hex2bin(const char *str);
int	 	 dctool_list_run(dc_context_t *context);
int		 dctool_download_run(dc_context_t *, dc_descriptor_t *, const char *);
int		 dctool_cancel_cb(void *userdata);
dctool_output_t *dctool_ss_output_new(dctool_units_t);
dc_status_t	 dctool_ss_output_write(dctool_output_t *, 
			dc_parser_t *, 
			const unsigned char[], unsigned int);
dc_status_t	 dctool_ss_output_free(dctool_output_t *);

extern int	 verbose;

__END_DECLS

#endif /* !EXTERN_H */
