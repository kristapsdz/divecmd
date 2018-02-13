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
#include "config.h"

#include <string.h>
#include <stdio.h>

#include "extern.h"

const char *
dctool_errmsg(dc_status_t status)
{

	switch (status) {
	case DC_STATUS_SUCCESS:
		return "success";
	case DC_STATUS_UNSUPPORTED:
		return "unsupported operation";
	case DC_STATUS_INVALIDARGS:
		return "invalid arguments";
	case DC_STATUS_NOMEMORY:
		return "out of memory";
	case DC_STATUS_NODEVICE:
		return "no device found";
	case DC_STATUS_NOACCESS:
		return "access denied";
	case DC_STATUS_IO:
		return "input/output error";
	case DC_STATUS_TIMEOUT:
		return "timeout";
	case DC_STATUS_PROTOCOL:
		return "protocol error";
	case DC_STATUS_DATAFORMAT:
		return "data format error";
	case DC_STATUS_CANCELLED:
		return "cancelled";
	default:
		return "unknown error";
	}
}

