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

struct	dcmd_out;

enum	dcmd_type {
	DC_OUTPUT_XML,
	DC_OUTPUT_LIST
};

__BEGIN_DECLS

const char	*dctool_errmsg(dc_status_t);

int		 download(dc_context_t *, dc_descriptor_t *, 
			const char *, enum dcmd_type, dc_buffer_t *, 
			dc_buffer_t *, dc_buffer_t **);

dc_status_t	 output_list_free(struct dcmd_out *);
struct dcmd_out *output_list_new(void);
dc_status_t	 output_list_write(struct dcmd_out *, 
			size_t, dc_parser_t *, const char *);

dc_status_t	 output_xml_free(struct dcmd_out *);
struct dcmd_out *output_xml_new(void);
dc_status_t	 output_xml_write(struct dcmd_out *, 
			size_t, dc_parser_t *, const char *);

int		 dctool_cancel_cb(void *userdata);

extern int	 verbose;

__END_DECLS

#endif /* !EXTERN_H */
