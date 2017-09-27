/*	$Id$ */
/*
 * Copyright (c) 2017 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <expat.h>

#include "parser.h"

enum	pmode {
	PMODE_JOIN,
	PMODE_SPLIT,
	PMODE_NONE
};

static	const char *events[EVENT__MAX] = {
	"none", /* EVENT_none */
	"decostop", /* EVENT_decostop */
	"rbt", /* EVENT_rbt */
	"ascent", /* EVENT_ascent */
	"ceiling", /* EVENT_ceiling */
	"workload", /* EVENT_workload */
	"transmitter", /* EVENT_transmitter */
	"violation", /* EVENT_violation */
	"bookmark", /* EVENT_bookmark */
	"surface", /* EVENT_surface */
	"safetystop", /* EVENT_safetystop */
	"gaschange", /* EVENT_gaschange */
	"safetystop_voluntary", /* EVENT_safetystop_voluntary */
	"safetystop_mandatory", /* EVENT_safetystop_mandatory */
	"deepstop", /* EVENT_deepstop */
	"ceiling_safetystop", /* EVENT_ceiling_safetystop */
	"floor", /* EVENT_floor */
	"divetime", /* EVENT_divetime */
	"maxdepth", /* EVENT_maxdepth */
	"olf", /* EVENT_olf */
	"po2", /* EVENT_po2 */
	"airtime", /* EVENT_airtime */
	"rgbm", /* EVENT_rgbm */
	"heading", /* EVENT_heading */
	"tissuelevel", /* EVENT_tissuelevel */
	"gaschange2", /* EVENT_gaschange2 */
};

int verbose = 0;

/*
 * Close a divelog entry.
 * Symmetrised by print_dlog_open().
 */
static void
print_dlog_close(void)
{

	puts("\t</dives>\n</divelog>");
}

/*
 * Open a divelog entry.
 * Prints all information, but overrides the version and program.
 * Symmetrised by print_dlog_close().
 */
static void
print_dlog_open(const struct dlog *dl)
{

	printf("<?xml version=\"1.0\" "
		"encoding=\"UTF-8\" ?>\n"
	       "<divelog program=\"divecmd2divecmd\" "
	        "version=\"" VERSION "\"");

	if (NULL != dl->ident)
		printf(" diver=\"%s\"", dl->ident);
	if (NULL != dl->product)
		printf(" product=\"%s\"", dl->product);
	if (NULL != dl->vendor)
		printf(" vendor=\"%s\"", dl->vendor);
	if (NULL != dl->model)
		printf(" model=\"%s\"", dl->model);

	puts(">\n\t<dives>");
}

static void
print_sample(const struct samp *s, size_t t)
{
	size_t	 i;

	t = 0 == t ? s->time : t;

	printf("\t\t\t\t<sample time=\"%zu\">\n", t);
	if (SAMP_DEPTH & s->flags)
		printf("\t\t\t\t\t"
		       "<depth value=\"%g\" />\n", s->depth);
	if (SAMP_TEMP & s->flags)
		printf("\t\t\t\t\t"
		       "<temp value=\"%g\" />\n", s->temp);
	if (SAMP_RBT & s->flags)
		printf("\t\t\t\t\t"
		       "<rbt value=\"%zu\" />\n", s->rbt);
	if (SAMP_EVENT & s->flags) {
		for (i = 0; i < EVENT__MAX; i++) {
			if ( ! ((1U << i) & s->events))
				continue;
			printf("\t\t\t\t\t"
			       "<event type=\"%s\" />\n", 
			       events[i]);
		}
	}
	puts("\t\t\t\t</sample>");
}

/*
 * See print_dive_open().
 */
static void
print_dive_close(void)
{

	puts("\t\t\t</samples>\n"
	     "\t\t</dive>");
}

/*
 * Print the heading for a particular dive.
 * Set the start date at "t", being dive "num", in mode "mode".
 * Don't print any other attributes of the dive.
 * (These are the critical ones.)
 * Follow with print_dive_close().
 */
static void
print_dive_open(time_t t, size_t num, enum mode mode)
{
	struct tm	*tm = localtime(&t);

	printf("\t\t<dive number=\"%zu\" "
		"date=\"%04d-%02d-%02d\" "
		"time=\"%02d:%02d:%02d\"",
		num, tm->tm_year + 1900, 
		tm->tm_mon + 1, tm->tm_mday, 
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	if (MODE_FREEDIVE == mode)
		printf(" mode=\"freedive\"");
	else if (MODE_GAUGE == mode)
		printf(" mode=\"gauge\"");
	else if (MODE_OC == mode)
		printf(" mode=\"opencircuit\"");
	else if (MODE_CC == mode)
		printf(" mode=\"closedcircuit\"");

	puts(">");
	puts("\t\t\t<samples>");
}

static void
print_dive(const struct dive *d)
{
	struct tm	  *tm;
	const struct samp *s;

	printf("\t\t<dive");

	if (d->num)
		printf(" number=\"%zu\"", d->num);

	if (d->datetime) {
		tm = localtime(&d->datetime);
		printf(" date=\"%04d-%02d-%02d\""
		       " time=\"%02d:%02d:%02d\"",
			tm->tm_year + 1900, 
			tm->tm_mon + 1, tm->tm_mday, 
			tm->tm_hour, tm->tm_min, tm->tm_sec);
	}

	if (MODE_FREEDIVE == d->mode)
		printf(" mode=\"freedive\"");
	else if (MODE_GAUGE == d->mode)
		printf(" mode=\"gauge\"");
	else if (MODE_OC == d->mode)
		printf(" mode=\"opencircuit\"");
	else if (MODE_CC == d->mode)
		printf(" mode=\"closedcircuit\"");

	puts(">");

	if (NULL != d->fprint)
		printf("\t\t\t<fingerprint>%s"
			"</fingerprint>\n", d->fprint);

	puts("\t\t\t<samples>");

	TAILQ_FOREACH(s, &d->samps, entries)
		print_sample(s, 0);

	puts("\t\t\t</samples>");
	puts("\t\t</dive>");
}

/*
 * Put all dive samples into the same dive profile.
 * We insert "top-side" dives during the surface interval between dive
 * sets.
 */
static void
print_join(const struct dive *d, time_t *last, time_t first)
{
	struct samp     *s1, *s2, *s;
	struct samp	 tmp;
	time_t           span;

	/* 
	 * First, compute how this dive computer records times (e.g.,
	 * once per second, etc.).
	 * We'll use this to create our fake entries.
	 */

	if (NULL == (s1 = TAILQ_FIRST(&d->samps)) ||
	    NULL == (s2 = TAILQ_NEXT(s1, entries)))
		return;

	assert(s1->time < s2->time);
	span = s2->time - s1->time;

	/*
	 * Print a fake sample for each spanned interval between the
	 * last dive (if specified) and the current one.
	 */

	if (*last && (*last += span) < d->datetime) {
		memset(&tmp, 0, sizeof(struct samp));
		tmp.flags |= SAMP_DEPTH;
		do {
			print_sample(&tmp, *last - first);
			*last += span;
		} while (*last < d->datetime);
	}

	/* Now print the samples. */

	TAILQ_FOREACH(s, &d->samps, entries) {
		print_sample(s, ((s->time + d->datetime) - first));
		*last = s->time + d->datetime;
	}
}

/*
 * Given a single dive entry, split it into multiple dive entries when
 * we have two consecutive dives <1 metre.
 * We then ignore samples until the next dive is >1 metre.
 */
static void
print_split(const struct dive *d, size_t *num, enum mode mode)
{
	time_t	 start;
	double	 lastdepth = 100.0;
	const struct samp *s;
	int	 dopen = 0;

	start = d->datetime;
	s = TAILQ_FIRST(&d->samps);
	assert(NULL != s);
again:
	for ( ; NULL != s; s = TAILQ_NEXT(s, entries)) {
		if ( ! (SAMP_DEPTH & s->flags) &&
		     ! (SAMP_TEMP & s->flags))
			continue;

		if (0 == dopen) {
			print_dive_open(start, (*num)++, mode);
			dopen = 1;
		}

		/* Print sample data. */

		print_sample(s, ((s->time + d->datetime) - start));

		/* Are we still "at depth"? */

		if ( ! (SAMP_DEPTH & s->flags))
			continue;

		if (lastdepth >= 1.0 || s->depth >= 1.0) {
			lastdepth = s->depth;
			continue;
		}

		print_dive_close();

		dopen = 0;

		/* 
		 * We're below depth for two in a row.
		 * Continue reading til we're back at depth. 
		 */

		for ( ; NULL != s; s = TAILQ_NEXT(s, entries)) {
			if ( ! (SAMP_DEPTH & s->flags))
				continue;
			start = d->datetime + s->time;
			if (s->depth >= 1.0)
				break;
		}
		goto again;
	}

	if (dopen)
		print_dive_close();
}

/*
 * Take a single input file and either split or join it.
 */
static int
print_all(enum pmode pmode, const struct dlog *dl, 
	const struct diveq *dq)
{
	const struct dive *d;
	size_t		 num = 1;
	time_t		 last, first;
	enum mode	 mode;

	if (PMODE_NONE == pmode) {
		print_dlog_open(dl);
		TAILQ_FOREACH(d, dq, entries)
			print_dive(d);
		print_dlog_close();
		return(1);
	}
	
	/* Grab the first divelog entry and use its data. */

	TAILQ_FOREACH(d, dq, entries)
		if (0 == d->datetime) {
			warnx("no dive timestamp"); 
			return(0);
		}

	print_dlog_open(dl);

	if (PMODE_SPLIT == pmode) {
		TAILQ_FOREACH(d, dq, entries)
			print_split(d, &num, d->mode);
	} else {
		d = TAILQ_FIRST(dq);
		assert (NULL != d);
		mode = d->mode;
		print_dive_open(d->datetime, 1, mode);
		first = d->datetime;
		last = 0;
		TAILQ_FOREACH(d, dq, entries) {
			if (d->mode != mode)
				warnx("mode mismatch");
			print_join(d, &last, first);
		}
		print_dive_close();
	}

	print_dlog_close();
	return(1);
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 1;
	size_t		 i;
	enum pmode	 mode = PMODE_NONE;
	XML_Parser	 p;
	struct diveq	 dq;
	struct dlog 	*dl;
	struct divestat	 st;

#if HAVE_PLEDGE
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (c = getopt(argc, argv, "jsv")))
		switch (c) {
		case ('j'):
			mode = PMODE_JOIN;
			break;
		case ('s'):
			mode = PMODE_SPLIT;
			break;
		case ('v'):
			verbose = 1;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	if (argc > 1)
		goto usage;

	divecmd_init(&p, &dq, &st, GROUP_NONE);

	if (0 == argc)
		rc = divecmd_parse("-", p, &dq, &st);

	for (i = 0; i < (size_t)argc; i++)
		if ( ! (rc = divecmd_parse(argv[i], p, &dq, &st)))
			break;

#if HAVE_PLEDGE
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	XML_ParserFree(p);

	if ( ! rc)
		goto out;

	if (TAILQ_EMPTY(&dq)) {
		warnx("no dives to display");
		goto out;
	}

	dl = TAILQ_FIRST(&st.dlogs);
	assert(NULL != dl);

	if (TAILQ_NEXT(dl, entries)) {
		/* FIXME: check if different. */
		warnx("multiple divelog entries (using first)");
		goto out;
	}

	rc = print_all(mode, dl, &dq);
out:
	divecmd_free(&dq, &st);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-jsv] [file ...]\n", getprogname());
	return(EXIT_FAILURE);
}
