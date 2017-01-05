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

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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

/*
 * Iterate over all descriptors in the system.
 * Return the one matching "name", which is presumed to be a product or
 * a vendor and product.
 * The first match returns.
 */
static dc_status_t
search_descr(dc_descriptor_t **out, const char *name)
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

/*
 * Iterate over all devices supported by libdivecomputer.
 */
static void
show_devices(void)
{
	dc_iterator_t	*iter = NULL;
	dc_descriptor_t *desc = NULL;
	dc_status_t	 st;
	const char	*vendor, *product;

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

    	st = dc_descriptor_iterator(&iter);
	if (DC_STATUS_SUCCESS != st)
		errx(EXIT_FAILURE, "%s", dctool_errmsg(st));

	while (DC_STATUS_SUCCESS == 
	       (st = dc_iterator_next(iter, &desc))) {
		vendor = dc_descriptor_get_vendor(desc);
		product = dc_descriptor_get_product(desc);
		printf("%s %s\n", 
			NULL == vendor ? "(none)" : vendor, 
			NULL == product ? "(none)" : product);
		dc_descriptor_free(desc);
	}

	if (DC_STATUS_DONE != st)
		errx(EXIT_FAILURE, "%s", dctool_errmsg(st));
	if (DC_STATUS_SUCCESS != (st = dc_iterator_free(iter)))
		errx(EXIT_FAILURE, "%s", dctool_errmsg(st));
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

static dc_buffer_t *
hex2bin(const char *str)
{
	size_t 		 i, nbytes = (str ? strlen (str) / 2 : 0);
	unsigned int	 in;
	unsigned char	 byte;
	dc_buffer_t 	*buffer;

	/* Get the length of the fingerprint data. */

	if (nbytes == 0)
		return NULL;

	/* Allocate and fill the memory buffer. */

	buffer = dc_buffer_new (nbytes);

	for (i = 0; i < nbytes; i++) {
		sscanf(&str[i * 2], "%02X", &in);
		assert(in <= UCHAR_MAX);
		byte = in;
		dc_buffer_append(buffer, &byte, 1);
	}

	return buffer;
}

static void
fprint_set(int fd, const char *file, dc_buffer_t *buf)
{
	ssize_t	 ssz;

	if (-1 == ftruncate(fd, 0))
		err(EXIT_FAILURE, "%s", file);
	ssz = write(fd, 
		dc_buffer_get_data(buf),
		dc_buffer_get_size(buf));
	if (-1 == ssz)
		err(EXIT_FAILURE, "%s", file);
	else if ((size_t)ssz != dc_buffer_get_size(buf))
		errx(EXIT_FAILURE, "%s: short write", file);
}

/*
 * Get the last-known fingerprint of "device".
 * Sets "fd" and "filep" to be the file descriptor and filename of the
 * fingerprint file, or -1 and NULL.
 * If device is NULL, nothing happens.
 * If the file was empty (e.g., just created), the return is NULL, but
 * the file is open anyway.
 */
static dc_buffer_t *
fprint_get(int *fd, char **filep, int all, const char *device)
{
	char		*cp, *ccp, *file;
	char		 buf[1024];
	dc_buffer_t	*res = NULL;
	struct stat	 st;
	size_t		 sz;
	ssize_t		 ssz;
	int		 rc;

	*fd = -1;
	*filep = NULL;

	if (NULL == device)
		return(NULL);

	/*
	 * First, make sure our directory exists.
	 * If it doesn't, create it.
	 */

	if (asprintf(&file, "%s/.divecmd", getenv("HOME")) < 0)
		err(EXIT_FAILURE, NULL);
	if (-1 == (rc = stat(file, &st)) && ENOENT != errno)
		err(EXIT_FAILURE, "%s", file);

	if (-1 == rc) {
		if (verbose)
			fprintf(stderr, "%s: creating\n", file);
		if (-1 == mkdir(file, 0755))
			err(EXIT_FAILURE, "%s", file);
	} else if ( ! (S_IFDIR & st.st_mode)) 
		errx(EXIT_FAILURE, "%s: not a directory", file);

	free(file);

	/* Escape device name as file. */

	if (NULL == (cp = strdup(device)))
		err(EXIT_FAILURE, NULL);
	for (ccp = cp; '\0' != *ccp; ccp++)
		if (isspace((int)*ccp) || '(' == *ccp || ')' == *ccp)
			*ccp = '_';
		else if (isalpha((int)*ccp))
			*ccp = tolower((int)*ccp);

	/* Open ~/.divecmd/DEVICE, exiting on new file or fail. */

	if (asprintf(&file, "%s/.divecmd/%s", getenv("HOME"), cp) < 0)
		err(EXIT_FAILURE, NULL);

	*filep = file;

	if (-1 == (*fd = open(file, O_CREAT|O_RDWR, 0644)))
		err(EXIT_FAILURE, "%s", file);
	else if (-1 == fstat(*fd, &st))
		err(EXIT_FAILURE, "%s", file);


	/* Empty file: we just created it? */

	if (0 == st.st_size) {
		if (verbose)
			fprintf(stderr, "%s: creating\n", file);
		goto out;
	}

	/* Check if don't read fingerprint. */

	if (all && verbose)
		fprintf(stderr, "%s: ignoring contents\n", file);
	if (all)
		goto out;

	/* Copy the entire file (fingerprint) into buffer. */

	sz = st.st_size;
	if (NULL == (res = dc_buffer_new(sz)))
		err(EXIT_FAILURE, NULL);

	for (;;) {
		if (-1 == (ssz = read(*fd, buf, sizeof(buf))))
			err(EXIT_FAILURE, "%s", file);
		else if (0 == ssz)
			break;
		if ( ! dc_buffer_append(res, buf, ssz))
			err(EXIT_FAILURE, NULL);
	}

	/* Clean up descriptors and temporary memory. */
out:
	if (NULL != res && verbose)
		fprintf(stderr, "%s: good fingerprint\n", file);
	if (NULL == res && verbose)
		fprintf(stderr, "%s: no fingerprint\n", file);
		
	free(cp);
	return(res);
}

int
main(int argc, char *argv[])
{
	int		 exitcode = 0;
	dc_status_t	 status = DC_STATUS_SUCCESS;
	dc_context_t	*context = NULL;
	dc_descriptor_t *descriptor = NULL;
	dc_loglevel_t 	 loglevel = DC_LOGLEVEL_WARNING;
	const char 	*device = NULL, *ofp = NULL;
	const char	*udev = "/dev/ttyU0";
	int 		 show = 0, ch, ofd = -1, nofp = 0, all = 0;
	dc_buffer_t	*fprint = NULL, *ofprint = NULL, *lprint = NULL;
	enum dcmd_type	 out = DC_OUTPUT_XML;
	char		*ofile = NULL;

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (ch = getopt (argc, argv, "ad:f:lnsv"))) {
		switch (ch) {
		case 'a':
			all = 1;
			break;
		case 'd':
			udev = optarg;
			break;
		case 'f':
			ofp = optarg;
			break;
		case 'l':
			out = DC_OUTPUT_LIST;
			break;
		case 'n':
			nofp = 1;
			break;
		case 's':
			show = 1;
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

	if (0 == show && 0 == argc)
		goto usage;
	else if (0 == show)
		device = argv[0];

	/* 
	 * Convert desired fingerprint into binary.
	 * Also read in the last-seen fingerprint.
	 * We'll [optionally] use these to constrain which dives we
	 * report to the operator.
	 */

	if (NULL != ofp) {
		ofprint = hex2bin(ofp);
		all = nofp = 1;
	}

	fprint = fprint_get(&ofd, &ofile, all, device);

	/* Setup the cancel signal handler. */

	signal(SIGINT, sighandler);

	/* Initialize a library context. */

	status = dc_context_new(&context);
	if (status != DC_STATUS_SUCCESS) {
		warnx("%s", dctool_errmsg(status));
		goto cleanup;
	}

	/* Setup the logging. */

	dc_context_set_loglevel(context, loglevel);
	dc_context_set_logfunc(context, logfunc, NULL);

	if (0 == show) {
		status = search_descr(&descriptor, device);
		if (status != DC_STATUS_SUCCESS) {
			warnx("%s", dctool_errmsg(status));
			goto cleanup;
		} else if (descriptor == NULL) {
			warnx("%s: not supported", device);
			goto cleanup;
		}
	}

	if (show) {
		/* We're only showing devices. */
		show_devices();
		exitcode = 1;
		goto cleanup;
	} 

	/*
	 * Do the full download and parse, setting the last fingerprint
	 * if it's found.
	 */

	exitcode = download
		(context, descriptor, udev, 
		 out, fprint, ofprint, &lprint);

	/* 
	 * If we have a last fingerprint, write it. 
	 * We do this by default, but it can be turned off: the operator
	 * says so (-n), the parse failed, or no fingerprint was parsed.
	 */

	if (exitcode && NULL != lprint && -1 != ofd) {
		if (nofp && verbose)
			fprintf(stderr, "%s: suppressing write\n", ofile);
		else if (0 == nofp)
			fprint_set(ofd, ofile, lprint);
	}

cleanup:
	if (-1 != ofd)
		close(ofd);
	dc_descriptor_free(descriptor);
	dc_context_free(context);
	dc_buffer_free(lprint);
	free(fprint);
	free(ofile);
	return(exitcode ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-anv] [-d device] [-f fingerprint] computer\n"
			"       %s [-v] -s\n",
			getprogname(), getprogname());
	return(EXIT_FAILURE);
}
