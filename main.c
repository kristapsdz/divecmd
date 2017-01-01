/*	$Id$ */
/*
 * Copyright (C) 2015 Jef Driesen
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libdivecomputer/context.h>
#include <libdivecomputer/descriptor.h>

#include "extern.h"

/* 
 * Program verbosity. 
 * Zero for quiet, 1 for some, 2 for more.
 */
int verbose = 0;

/* 
 * Cancellation (signal-control-c). 
 */
static volatile sig_atomic_t g_cancel = 0;

static dc_status_t
dctool_descriptor_search(dc_descriptor_t **out, const char *name)
{
	dc_status_t	 rc = DC_STATUS_SUCCESS;
	dc_iterator_t	*iterator = NULL;
	dc_descriptor_t *descriptor = NULL, *current = NULL;
	const char	*vendor, *product;
	size_t		 n;

	rc = dc_descriptor_iterator(&iterator);
	if (rc != DC_STATUS_SUCCESS) {
		warnx("%s", dctool_errmsg(rc));
		return(rc);
	}

	while ((rc = dc_iterator_next(iterator, &descriptor)) == 
	       DC_STATUS_SUCCESS) {
		vendor = dc_descriptor_get_vendor(descriptor);
		product = dc_descriptor_get_product(descriptor);
		n = strlen(vendor);

		if (strncasecmp(name, vendor, n) == 0 && 
		    name[n] == ' ' &&
		    strcasecmp (name + n + 1, product) == 0) {
			current = descriptor;
			break;
		} else if (strcasecmp (name, product) == 0) {
			current = descriptor;
			break;
		}

		dc_descriptor_free(descriptor);
	}

	if (rc != DC_STATUS_SUCCESS && rc != DC_STATUS_DONE) {
		dc_descriptor_free(current);
		dc_iterator_free(iterator);
		warnx("%s", dctool_errmsg(rc));
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
		warnx("%s: %s [in %s:%d (%s)]", 
			loglevels[loglevel], msg, file, line, func);
	else
		warnx("%s: %s", loglevels[loglevel], msg);
}

#if 0
static unsigned char
hex2dec (unsigned char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	else if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;
	else if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	else
		return 0;
}

dc_buffer_t *
dctool_convert_hex2bin (const char *str)
{
	size_t 		 nbytes = (str ? strlen (str) / 2 : 0);
	unsigned int	 i;
	unsigned char	 msn, lsn, byte;

	/* Get the length of the fingerprint data. */

	if (nbytes == 0)
		return NULL;

	/* Allocate a memory buffer. */

	dc_buffer_t *buffer = dc_buffer_new (nbytes);

	// Convert the hexadecimal string.
	for (i = 0; i < nbytes; ++i) {
		msn = hex2dec (str[i * 2 + 0]);
		lsn = hex2dec (str[i * 2 + 1]);
		byte = (msn << 4) + lsn;

		dc_buffer_append (buffer, &byte, 1);
	}

	return buffer;
}
#endif

static char *
fingerprint_get(const char *device)
{
	char		*cp, *ccp, *file, *res = NULL;
	struct stat	 st;
	size_t		 i, sz;
	ssize_t		 ssz;
	int		 fd = -1;

	/* Escape device name as file. */

	if (NULL == (cp = strdup(device)))
		err(EXIT_FAILURE, NULL);
	for (ccp = cp; '\0' != *ccp; ccp++)
		if (isspace((int)*ccp) || '(' == *ccp || ')' == *ccp)
			*ccp = '_';

	/* Open ~/.divecmd/DEVICE read-only, exiting on fail. */

	if (asprintf(&file, "%s/.divecmd/%s", getenv("HOME"), cp) < 0)
		err(EXIT_FAILURE, NULL);
	if (-1 == (fd = open(file, O_RDONLY, 0)))
		goto out;
	else if (-1 == fstat(fd, &st))
		err(EXIT_FAILURE, "%s", file);

	/* Copy the entire file (fingerprint) into buffer. */

	sz = st.st_size;
	res = malloc(sz + 1);
	res[sz] = '\0';

	if (-1 == (ssz = read(fd, res, sz)))
		err(EXIT_FAILURE, "%s", file);
	else if ((size_t)ssz != sz)
		errx(EXIT_FAILURE, "%s: short read", file);

	for (i = 0; i < sz; i++)
		if ( ! isalnum((int)res[i]))
			errx(EXIT_FAILURE, "%s: bad print", file);

out:
	if (NULL != res && verbose > 0)
		fprintf(stderr, "%s: good fingerprint\n", file);
	if (NULL == res && verbose > 0)
		fprintf(stderr, "%s: no fingerprint\n", file);
	if (-1 != fd)
		close(fd);
	free(cp);
	free(file);
	return(res);
}

int
main(int argc, char *argv[])
{
	int		 exitcode = EXIT_FAILURE;
	dc_status_t	 status = DC_STATUS_SUCCESS;
	dc_context_t	*context = NULL;
	dc_descriptor_t *descriptor = NULL;
	dc_loglevel_t 	 loglevel = DC_LOGLEVEL_WARNING;
	const char 	*device = NULL;
	const char	*udev = "/dev/ttyU0";
	int 		 list = 0, ch;
	char		*fprint;

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (ch = getopt (argc, argv, "d:lv"))) {
		switch (ch) {
		case 'd':
			device = optarg;
			break;
		case 'l':
			list = 1;
			break;
		case 'v':
			if (verbose)
				loglevel++;
			verbose++;
			break;
		default:
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	if (0 == list && NULL == device)
		goto usage;
	else if (0 == list && argc)
		udev = argv[0];

	fprint = fingerprint_get(device);

	/* Setup the cancel signal handler. */

	signal (SIGINT, sighandler);

	/* Initialize a library context. */

	status = dc_context_new(&context);
	if (status != DC_STATUS_SUCCESS) {
		warnx("%s", dctool_errmsg(status));
		goto cleanup;
	}

	/* Setup the logging. */

	dc_context_set_loglevel(context, loglevel);
	dc_context_set_logfunc(context, logfunc, NULL);

	if (0 == list) {
		status = dctool_descriptor_search
			(&descriptor, device);
		if (status != DC_STATUS_SUCCESS) {
			warnx("%s", dctool_errmsg(status));
			goto cleanup;
		} else if (descriptor == NULL) {
			warnx("%s: not supported", device);
			goto cleanup;
		}
	}

	exitcode = list ?
		dctool_list_run(context) :
		dctool_download_run(context, descriptor, udev);

cleanup:
	dc_descriptor_free(descriptor);
	dc_context_free(context);
	free(fprint);
	return(exitcode);
usage:
	fprintf(stderr, "usage: %s [-v] [-d computer] [device]\n"
			"       %s [-v] -l\n",
			getprogname(), getprogname());
	return(EXIT_FAILURE);
}
