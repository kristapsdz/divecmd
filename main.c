/*	$Id$ */
/*
 * Copyright (C) 2015 Jef Driesen
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
#ifdef __OpenBSD_
# include <sys/param.h>
#endif

#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libdivecomputer/context.h>
#include <libdivecomputer/descriptor.h>

#include "extern.h"

typedef struct backend_table_t {
	const char *name;
	dc_family_t type;
	unsigned int model;
} backend_table_t;

static const backend_table_t g_backends[] = {
	{"solution",    DC_FAMILY_SUUNTO_SOLUTION,     0},
	{"eon",	        DC_FAMILY_SUUNTO_EON,          0},
	{"vyper",       DC_FAMILY_SUUNTO_VYPER,        0x0A},
	{"vyper2",      DC_FAMILY_SUUNTO_VYPER2,       0x10},
	{"d9",          DC_FAMILY_SUUNTO_D9,           0x0E},
	{"eonsteel",    DC_FAMILY_SUUNTO_EONSTEEL,     0},
	{"aladin",      DC_FAMILY_UWATEC_ALADIN,       0x3F},
	{"memomouse",   DC_FAMILY_UWATEC_MEMOMOUSE,    0},
	{"smart",       DC_FAMILY_UWATEC_SMART,        0x10},
	{"meridian",    DC_FAMILY_UWATEC_MERIDIAN,     0x20},
	{"sensus",      DC_FAMILY_REEFNET_SENSUS,      1},
	{"sensuspro",   DC_FAMILY_REEFNET_SENSUSPRO,   2},
	{"sensusultra", DC_FAMILY_REEFNET_SENSUSULTRA, 3},
	{"vtpro",       DC_FAMILY_OCEANIC_VTPRO,       0x4245},
	{"veo250",      DC_FAMILY_OCEANIC_VEO250,      0x424C},
	{"atom2",       DC_FAMILY_OCEANIC_ATOM2,       0x4342},
	{"nemo",        DC_FAMILY_MARES_NEMO,          0},
	{"puck",        DC_FAMILY_MARES_PUCK,          7},
	{"darwin",      DC_FAMILY_MARES_DARWIN,        0},
	{"iconhd",      DC_FAMILY_MARES_ICONHD,        0x14},
	{"ostc",        DC_FAMILY_HW_OSTC,             0},
	{"frog",        DC_FAMILY_HW_FROG,             0},
	{"ostc3",       DC_FAMILY_HW_OSTC3,            0x0A},
	{"edy",         DC_FAMILY_CRESSI_EDY,          0x08},
	{"leonardo",	DC_FAMILY_CRESSI_LEONARDO,     1},
	{"n2ition3",    DC_FAMILY_ZEAGLE_N2ITION3,     0},
	{"cobalt",      DC_FAMILY_ATOMICS_COBALT,      0},
	{"predator",	DC_FAMILY_SHEARWATER_PREDATOR, 2},
	{"petrel",      DC_FAMILY_SHEARWATER_PETREL,   3},
	{"nitekq",      DC_FAMILY_DIVERITE_NITEKQ,     0},
	{"aqualand",    DC_FAMILY_CITIZEN_AQUALAND,    0},
	{"idive",       DC_FAMILY_DIVESYSTEM_IDIVE,    0x03},
	{"cochran",     DC_FAMILY_COCHRAN_COMMANDER,   0},
	{NULL,		DC_FAMILY_NULL,		       0},
};

static volatile sig_atomic_t g_cancel = 0;

static dc_family_t
dctool_family_type(const char *name)
{
	size_t	 i;

	for (i = 0; NULL != g_backends[i].name; i++)
		if (0 == strcmp(name, g_backends[i].name))
			return(g_backends[i].type);

	return(DC_FAMILY_NULL);
}

static const char *
dctool_family_name(dc_family_t type)
{
	size_t	 i;

	for (i = 0; NULL != g_backends[i].name; i++) 
		if (g_backends[i].type == type)
			return(g_backends[i].name);

	return(NULL);
}

static unsigned int
dctool_family_model(dc_family_t type)
{
	size_t	 i;

	for (i = 0; NULL != g_backends[i].name; i++)
		if (g_backends[i].type == type)
			return(g_backends[i].model);

	return(0);
}

static dc_status_t
dctool_descriptor_search(dc_descriptor_t **out, 
	const char *name, dc_family_t family, unsigned int model)
{
	dc_status_t	 rc = DC_STATUS_SUCCESS;
	dc_iterator_t	*iterator = NULL;
	dc_descriptor_t *descriptor = NULL, *current = NULL;
	const char	*vendor, *product;
	size_t		 n;

	rc = dc_descriptor_iterator(&iterator);
	if (rc != DC_STATUS_SUCCESS) {
		warnx("dc_descriptor_iterator: %s", dctool_errmsg(rc));
		return(rc);
	}

	while ((rc = dc_iterator_next(iterator, &descriptor)) == DC_STATUS_SUCCESS) {
		if (NULL != name) {
			vendor = dc_descriptor_get_vendor (descriptor);
			product = dc_descriptor_get_product (descriptor);
			n = strlen(vendor);

			if (strncasecmp(name, vendor, n) == 0 && name[n] == ' ' &&
			    strcasecmp (name + n + 1, product) == 0) {
				current = descriptor;
				break;
			} else if (strcasecmp (name, product) == 0) {
				current = descriptor;
				break;
			}
		} else {
			if (family == dc_descriptor_get_type(descriptor)) {
				if (model == dc_descriptor_get_model(descriptor)) {
					/* Exact match found. */
					dc_descriptor_free (current);
					current = descriptor;
					break;
				} else {
					/* 
					 * Possible match found. Keep
					 * searching for an exact match.
					 * If no exact match is found,
					 * the first match is returned.
					 */
					if (current == NULL) {
						current = descriptor;
						descriptor = NULL;
					}
				}
			}
		}

		dc_descriptor_free(descriptor);
	}

	if (rc != DC_STATUS_SUCCESS && rc != DC_STATUS_DONE) {
		dc_descriptor_free(current);
		dc_iterator_free(iterator);
		warnx("dc_iterator_next: %s", dctool_errmsg(rc));
		return(rc);
	}

	dc_iterator_free(iterator);
	*out = current;
	return DC_STATUS_SUCCESS;
}

int
dctool_cancel_cb(void *dat)
{

	(void)dat;
	return(g_cancel);
}

static void
sighandler(int signum)
{

	/* Restore the default signal handler. */

	signal(signum, SIG_DFL);
	g_cancel = 1;
}

/*
 * Log routine passed to libdivecomputer.
 */
static void
logfunc(dc_context_t *ctx, dc_loglevel_t loglevel, 
	const char *file, unsigned int line, 
	const char *func, const char *msg, void *dat)
{
	const char *loglevels[] = {
		"NONE", "ERROR", "WARNING", "INFO", "DEBUG", "ALL"
	};

	(void)dat;
	(void)ctx;

	if (loglevel == DC_LOGLEVEL_ERROR || 
	    loglevel == DC_LOGLEVEL_WARNING)
		warnx("%s: %s [in %s:%d (%s)]\n", 
			loglevels[loglevel], msg, file, line, func);
	else
		warnx("%s: %s\n", loglevels[loglevel], msg);
}

int
main (int argc, char *argv[])
{
	int		 exitcode = EXIT_FAILURE;
	dc_status_t	 status = DC_STATUS_SUCCESS;
	dc_context_t	*context = NULL;
	dc_descriptor_t *descriptor = NULL;
	dc_loglevel_t 	 loglevel = DC_LOGLEVEL_WARNING;
	const char 	*device = NULL;
	const char	*udev = NULL;
	dc_family_t 	 family = DC_FAMILY_NULL;
	unsigned int 	 model = 0;
	unsigned int 	 have_family = 0, have_model = 0;
	int 		 list = 0, ch;

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (ch = getopt (argc, argv, "d:f:m:lqv"))) {
		switch (ch) {
		case 'd':
			device = optarg;
			break;
		case 'f':
			family = dctool_family_type(optarg);
			have_family = 1;
			break;
		case 'l':
			list = 1;
			break;
		case 'm':
			model = strtoul(optarg, NULL, 0);
			have_model = 1;
			break;
		case 'q':
			loglevel = DC_LOGLEVEL_NONE;
			break;
		case 'v':
			loglevel++;
			break;
		default:
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	if (0 == list && 0 == argc)
		goto usage;
	else if (0 == list)
		udev = argv[0];

	/* Set the default model number. */

	if (have_family && ! have_model)
		model = dctool_family_model(family);

	/* Setup the cancel signal handler. */

	signal (SIGINT, sighandler);

	/* Initialize a library context. */

	status = dc_context_new (&context);
	if (status != DC_STATUS_SUCCESS)
		goto cleanup;

	/* Setup the logging. */

	dc_context_set_loglevel(context, loglevel);
	dc_context_set_logfunc(context, logfunc, NULL);

	if (0 == list) {
		if (device == NULL && 
		    family == DC_FAMILY_NULL) {
			warnx("-d or -f required");
			goto cleanup;
		}

		/* Search for a matching device descriptor. */

		status = dctool_descriptor_search
			(&descriptor, device, family, model);
		if (status != DC_STATUS_SUCCESS)
			goto cleanup;

		/* Fail if no device descriptor found. */

		if (descriptor == NULL) {
			if (NULL != device)
				warnx("%s: device not "
					"supported", device);
			else
				warnx("%s: model not "
					"supported: 0x%X",
					dctool_family_name(family), 
					model);
			goto cleanup;
		}
	}

	exitcode = list ?
		dctool_list_run(context) :
		dctool_download_run(context, descriptor, udev);

cleanup:
	dc_descriptor_free(descriptor);
	dc_context_free(context);
	return(exitcode);
usage:
	fprintf(stderr, "usage: %s [-qv] [-d computer] "
			"[-f family] [-m model] device\n"
			"       %s [-qv] -l\n",
			getprogname(), getprogname());
	return(EXIT_FAILURE);
}
