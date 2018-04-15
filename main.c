/*	$Id$ */
/*
 * Copyright (C) 2015 Jef Driesen
 * Copyright (c) 2016--2017 Kristaps Dzonsons <kristaps@bsd.lv>
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

#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
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
 * Clone of dc_descriptor_t (opaque).
 */
struct	descr {
	char		*vend; /* vendor */
	char		*prod; /* product */
	unsigned int	 model; /* model number */
};

/*
 * Iterate over all descriptors in the system.
 * Return the one matching "name", which is presumed to be a product or
 * a vendor/product pair.
 * The first match returns.
 */
static int
search_descr(dc_descriptor_t **out, const char *name, 
	unsigned int mod, int has_mod)
{
	dc_status_t	 rc = DC_STATUS_SUCCESS;
	dc_iterator_t	*iterator = NULL;
	dc_descriptor_t *descriptor = NULL, *current = NULL;
	const char	*vendor, *product;
	size_t		 n;
	unsigned int	 model;

	rc = dc_descriptor_iterator(&iterator);
	if (rc != DC_STATUS_SUCCESS) {
		warnx("%s", dctool_errmsg(rc));
		return(0);
	}

	while ((rc = dc_iterator_next(iterator, &descriptor)) == 
	       DC_STATUS_SUCCESS) {
		vendor = dc_descriptor_get_vendor(descriptor);
		product = dc_descriptor_get_product(descriptor);
		model = dc_descriptor_get_model(descriptor);

		n = strlen(vendor);

		/*
		 * First look up by vendor/product pair (vendor followed
		 * by a space, then the product).
		 * If included on the command-line, we also look for the
		 * model number.
		 */

		if (strncasecmp(name, vendor, n) == 0 && 
		    name[n] == ' ' &&
		    strcasecmp(name + n + 1, product) == 0 &&
		    ( ! has_mod || mod == model)) {
			current = descriptor;
			break;
		} 
		
		/*
		 * Next, just the product name.
		 * Again, if specified, use the model number.
		 */

		if (strcasecmp(name, product) == 0 &&
		    ( ! has_mod || mod == model)) {
			current = descriptor;
			break;
		}

		dc_descriptor_free(descriptor);
	}

	if (rc != DC_STATUS_SUCCESS && rc != DC_STATUS_DONE) {
		dc_descriptor_free(current);
		dc_iterator_free(iterator);
		warnx("%s", dctool_errmsg(rc));
		return(0);
	}

	dc_iterator_free(iterator);
	*out = current;
	return(1);
}

/*
 * Sort descriptors by vendor, then product, then model no.
 */
static int
descr_cmp(const void *p1, const void *p2)
{
	const struct descr *d1 = p1, *d2 = p2;
	int	 c;

	if (0 != (c = strcmp(d1->vend, d2->vend)))
		return(c);
	if (0 != (c = strcmp(d1->prod, d2->prod)))
		return(c);
	return(d1->model - d2->model);
}

/*
 * Iterate over all devices supported by libdivecomputer.
 * We do this in two phases: fill an array of computers, then print
 * them.
 * This is to sort them in order to print which ones need model numbers,
 * i.e., which have multiple products of the same name but with
 * different model numbers.
 */
static void
show_devices(void)
{
	dc_iterator_t	*iter = NULL;
	dc_descriptor_t *desc = NULL;
	dc_status_t	 st;
	struct descr	*ds = NULL;
	size_t		 maxds = 0, dsz = 0, i;

#if HAVE_PLEDGE
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	/* Start with 200 supported computers. */

	maxds = 200;
	if (NULL == (ds = calloc(maxds, sizeof(struct descr))))
		err(EXIT_FAILURE, NULL);

    	st = dc_descriptor_iterator(&iter);
	if (DC_STATUS_SUCCESS != st)
		errx(EXIT_FAILURE, "%s", dctool_errmsg(st));

	/* Gather all computers into an array. */

	while (DC_STATUS_SUCCESS == 
	       (st = dc_iterator_next(iter, &desc))) {
		if (dsz == maxds) {
			/* Double the array size. */
			maxds *= 2;
			ds = reallocarray(ds, maxds, 
				sizeof(struct descr));
			if (NULL == ds)
				err(EXIT_FAILURE, NULL);
		}
		ds[dsz].vend = strdup
			(dc_descriptor_get_vendor(desc));
		ds[dsz].prod = strdup
			(dc_descriptor_get_product(desc));
		if (NULL == ds[dsz].vend || 
		    NULL == ds[dsz].prod)
			err(EXIT_FAILURE, NULL);
		ds[dsz].model = dc_descriptor_get_model(desc);
		dc_descriptor_free(desc);
		dsz++;
	}

	if (DC_STATUS_DONE != st)
		errx(EXIT_FAILURE, "%s", dctool_errmsg(st));
	if (DC_STATUS_SUCCESS != (st = dc_iterator_free(iter)))
		errx(EXIT_FAILURE, "%s", dctool_errmsg(st));

	/* 
	 * Make sure we're sorted to see if we have duplicated vendor
	 * and product name. 
	 */

	qsort(ds, dsz, sizeof(struct descr), descr_cmp);

	/*
	 * If the prior or next computer has the same name and vendor,
	 * then print the model number as well.
	 */

	for (i = 0; i < dsz; i++) 
		if ((i > 0 && 
		     0 == strcmp(ds[i].vend, ds[i - 1].vend) &&
		     0 == strcmp(ds[i].prod, ds[i - 1].prod)) ||
		    (i < dsz - 1 &&
		     0 == strcmp(ds[i].vend, ds[i + 1].vend) &&
		     0 == strcmp(ds[i].prod, ds[i + 1].prod)))
			printf("%s %s (model %u)\n", ds[i].vend, 
				ds[i].prod, ds[i].model);
		else
			printf("%s %s\n", ds[i].vend, ds[i].prod);

	for (i = 0; i < dsz; i++) {
		free(ds[i].prod);
		free(ds[i].vend);
	}

	free(ds);
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

	if (-1 == lseek(fd, 0, SEEK_SET))
		err(EXIT_FAILURE, "%s", file);
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
 * Get the last-known fingerprint of "desc".
 * Sets "fd" and "filep" to be the file descriptor and filename of the
 * fingerprint file, or -1 and NULL.
 * If the file was empty (e.g., just created), the return is NULL, but
 * the file is open anyway.
 */
static dc_buffer_t *
fprint_get(int *fd, char **filep, int all, 
	dc_descriptor_t *desc, const char *ident)
{
	char		*cp, *ccp, *file;
	unsigned char	 buf[1024];
	dc_buffer_t	*res = NULL;
	struct stat	 st;
	size_t		 sz;
	ssize_t		 ssz;
	int		 rc;

	*fd = -1;
	*filep = NULL;

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

	rc = NULL != ident ?
		asprintf(&cp, "%s %s-%u", 
			dc_descriptor_get_vendor(desc),
			dc_descriptor_get_product(desc),
			dc_descriptor_get_model(desc)) :
		asprintf(&cp, "%s %s-%u %s", 
			dc_descriptor_get_vendor(desc),
			dc_descriptor_get_product(desc),
			dc_descriptor_get_model(desc),
			ident);

	if (rc < 0)
		err(EXIT_FAILURE, NULL);

	/* Replace whitespace and path delimiters. */

	for (ccp = cp; '\0' != *ccp; ccp++)
		if (isspace((int)*ccp) || '/' == *ccp)
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

/*
 * Convert the date-time (or just time) in "val" into the broken-down
 * local time in "t".
 * The format is yyyy-mm-dd[Thh:mm:ss].
 * There is no time-zone designation.
 */
static int
parsedatetime(const char *val, dc_datetime_t *t)
{
	int	 rc, hastime;

	memset(t, 0, sizeof(dc_datetime_t));

	hastime = NULL != strchr(val, 'T');

	rc = hastime ?
		sscanf(val, "%d-%d-%dT%d:%d:%d",
			&t->year, &t->month,
			&t->day, &t->hour,
			&t->minute, &t->second) :
		sscanf(val, "%d-%d-%d",
			&t->year, &t->month, &t->day);

	return((hastime && 6 == rc) || (0 == hastime && 3 == rc));
}

/*
 * Parse a date range, which looks like:
 * [start][/[end]]
 * If there's nothing at all, then assume today.
 * If there's just a start date, then the end date is 24 hours after the
 * start date.
 * If there's a range and the start date is empty, it starts from the
 * beginning of time.
 * If there's no end to the range, it goes til now.
 */
static int
parserange(const char *range, struct dcmd_rng **pp)
{
	char		*cp, *start, *end;
	dc_datetime_t	 tstart, tend;
	dc_ticks_t	 now;
	struct dcmd_rng	*p;

	if (NULL == (start = cp = strdup(range)))
		err(EXIT_FAILURE, NULL);

	if (NULL == (p = calloc(1, sizeof(struct dcmd_rng))))
		err(EXIT_FAILURE, NULL);

	/* No date at all?  Just today's dives. */

	if ('\0' == *start) {
		now = dc_datetime_now();
		if (NULL == dc_datetime_localtime(&tstart, now))
			err(EXIT_FAILURE, NULL);
		tstart.hour = tstart.minute = tstart.second = 0;
		if ((p->start = dc_datetime_mktime(&tstart)) < 0)
			err(EXIT_FAILURE, NULL);
		p->end = p->start + 24 * 60 * 60;
		free(cp);
		*pp = p;
		return(1);
	}

	if (NULL != (end = strchr(start, '/'))) 
		*end++ = '\0';

	/* 
	 * If we don't have a start date, then we start at the beginning
	 * of time.
	 * Otherwise, actually parse the start date.
	 */

	if ('\0' != *start) {
		if ( ! parsedatetime(start, &tstart)) {
			warnx("failed to parse range start");
			free(cp);
			free(p);
			return(0);
		} 
		if ((p->start = dc_datetime_mktime(&tstart)) < 0)
			errx(EXIT_FAILURE, "%s: bad date", start);
	}

	/*
	 * If we don't have an end date at all, then we're only going to
	 * look for the start date's day, so make the end date the start
	 * date plus 24 hours.
	 * However, if we have a range specifier, but the end date is
	 * blank, then assume we mean til now.
	 */

	if (NULL == end) {
		p->end = p->start + 24 * 60 * 60;
		*pp = p;
		free(cp);
		return(1);
	} else if ('\0' == *end) {
		if ((p->end = dc_datetime_now()) < 0)
			err(EXIT_FAILURE, NULL);
		*pp = p;
		free(cp);
		return(1);
	}

	if ( ! parsedatetime(end, &tend)) {
		warnx("failed to parse range end");
		free(cp);
		return(0);
	} 

	if ((p->end = dc_datetime_mktime(&tend)) < 0)
		errx(EXIT_FAILURE, "%s: bad date", end);

	*pp = p;
	free(cp);
	return(1);
}

int
main(int argc, char *argv[])
{
	int		 exitcode = 0;
	dc_status_t	 status = DC_STATUS_SUCCESS;
	dc_context_t	*context = NULL;
	dc_descriptor_t *descriptor = NULL;
	dc_loglevel_t 	 loglevel = DC_LOGLEVEL_WARNING;
	const char 	*ofp = NULL, *range = NULL, *ident = NULL, *er;
	const char	*udev = "/dev/ttyU0";
	int 		 show = 0, ch, ofd = -1, nofp = 0, all = 0;
	dc_buffer_t	*fprint = NULL, *ofprint = NULL, *lprint = NULL;
	enum dcmd_type	 out = DC_OUTPUT_XML;
	char		*ofile = NULL;
	struct dcmd_rng	*rng = NULL;
	unsigned int	 model;
	int		 has_model = 0;

	while (-1 != (ch = getopt (argc, argv, "ad:f:i:lm:nr:sv"))) {
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
		case 'i':
			ident = optarg;
			break;
		case 'l':
			out = DC_OUTPUT_LIST;
			break;
		case 'm':
			model = strtonum(optarg, 0, UINT_MAX, &er);
			if (NULL == er) {
				has_model = 1;
				break;
			}
			errx(EXIT_FAILURE, "-m: %s", er);
			/* NOTREACHED */
		case 'n':
			nofp = 1;
			break;
		case 'r':
			range = optarg;
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

	/* 
	 * If we're only showing our available devices, then do so now
	 * as this uses no resources.
	 */

	if (show) {
		show_devices();
		exitcode = 1;
		goto cleanup;
	} else if (0 == argc)
		goto usage;

	/* Look up the device in our descriptor table. */

	if ( ! search_descr(&descriptor, argv[0], model, has_model))
		goto cleanup;

	if (descriptor == NULL) {
		warnx("%s: unknown dive computer", argv[0]);
		goto cleanup;
	}

	/* If applicable, get desired fingerprint. */

	if (NULL != ofp) {
		ofprint = hex2bin(ofp);
		range = NULL;
		all = nofp = 1;
	}

	/* If applicable, get range-check values. */

	if (NULL != range && parserange(range, &rng))
		all = nofp = 1;

	/* Deserialise last-seen fingerprint. */

	fprint = fprint_get(&ofd, &ofile, all, descriptor, ident);

	/* Setup the cancel signal handler. */

	if (SIG_ERR == signal(SIGINT, sighandler))
		err(EXIT_FAILURE, NULL);

	/* Initialize a library context. */

	status = dc_context_new(&context);
	if (status != DC_STATUS_SUCCESS) {
		warnx("%s", dctool_errmsg(status));
		goto cleanup;
	}

	/* Setup the logging. */

	dc_context_set_loglevel(context, loglevel);
	dc_context_set_logfunc(context, logfunc, NULL);

	/*
	 * Do the full download and parse, setting the last fingerprint
	 * if it's found, given our range constraints, last-seen
	 * fingerprint constraint, and single-fingerprint constraint.
	 */

	exitcode = download
		(context, descriptor, udev, 
		 out, fprint, ofprint, &lprint, rng, ident);

	/* Serialise last fingerprint if found & enabled. */

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
	dc_buffer_free(fprint);
	dc_buffer_free(ofprint);
	free(ofile);
	free(rng);
	return(exitcode ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-anv] [-d device] "
				  "[-f fingerprint] "
				  "[-i identity] "
				  "[-m model] computer\n"
			"       %s [-v] -s\n",
			getprogname(), getprogname());
	return(EXIT_FAILURE);
}
