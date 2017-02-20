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
#ifdef __OpenBSD_
# include <sys/param.h>
#endif
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <expat.h>

#include "parser.h"

enum	pmode {
	PMODE_JOIN,
	PMODE_SPLIT
};

int verbose = 0;

static void
print_dive_open(time_t t, size_t num, const char *mode)
{
	struct tm	*tm = localtime(&t);

	printf("\t\t<dive number=\"%zu\" "
		"date=\"%04d-%02d-%02d\" "
		"time=\"%02d:%02d:%02d\"",
		num++, tm->tm_year + 1900, 
		tm->tm_mon + 1, tm->tm_mday, 
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	if (NULL != mode)
		printf(" mode=\"%s\"", mode);

	puts(">");
	puts("\t\t\t<samples>");
}

/*
 * Given a single dive entry, split it into multiple dive entries when
 * we have two consecutive dives <1 metre.
 * We then ignore samples until the next dive is >1 metre.
 */
static void
print_split(const struct dive *d, size_t *num, const char *mode)
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

		printf("\t\t\t\t<sample time=\"%lld\">\n",
			(long long)((s->time + d->datetime) - start));
		if (SAMP_DEPTH & s->flags)
			printf("\t\t\t\t\t<depth value=\"%g\" />\n",
				s->depth);
		if (SAMP_TEMP & s->flags)
			printf("\t\t\t\t\t<temp value=\"%g\" />\n",
				s->temp);
		puts("\t\t\t\t</sample>");

		/* Are we still "at depth"? */

		if ( ! (SAMP_DEPTH & s->flags))
			continue;

		if (lastdepth >= 1.0 || s->depth >= 1.0) {
			lastdepth = s->depth;
			continue;
		}

		puts("\t\t\t</samples>");
		puts("\t\t</dive>");
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

	if (dopen) {
		puts("\t\t\t</samples>");
		puts("\t\t</dive>");
	}
}

static int
print_all(enum pmode pmode, const struct diveq *dq)
{
	const struct dive *d;
	const struct dlog *dl;
	size_t		 num = 0;
	const char	*mode;
	
	/* Grab the first divelog entry and use its data. */

	d = TAILQ_FIRST(dq);
	assert(NULL != d);
	dl = d->log;
	assert(NULL != dl);

	TAILQ_FOREACH(d, dq, entries)
		if (0 == d->datetime) {
			warnx("no dive timestamp"); 
			return(0);
		}

	printf("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
	       "<divelog program=\"divecmd2divecmd\" "
	        "version=\"" VERSION "\"");

	if (NULL != dl->ident)
		printf(" diver=\"%s\"", dl->ident);

	puts(">\n\t<dives>");

	if (PMODE_SPLIT == pmode) {
		TAILQ_FOREACH(d, dq, entries) {
			if (MODE_FREEDIVE == d->mode)
				mode = "freedive";
			else if (MODE_GAUGE == d->mode)
				mode = "gauge";
			else if (MODE_OC == d->mode)
				mode = "opencircuit";
			else if (MODE_CC == d->mode)
				mode = "closedcircuit";
			else
				mode = NULL;
			print_split(d, &num, mode);
		}
	}

	puts("\t</dives>\n</divelog>");
	return(1);
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 1;
	enum pmode	 mode = PMODE_SPLIT;
	XML_Parser	 p;
	struct diveq	 dq;
	struct divestat	 st;

#if defined(__OpenBSD__) && OpenBSD > 201510
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

	parse_init(&p, &dq, &st, GROUP_NONE);

	rc = 0 == argc ?
		parse("-", p, &dq, &st) :
		parse(argv[0], p, &dq, &st);

#if defined(__OpenBSD__) && OpenBSD > 201510
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

	rc = print_all(mode, &dq);
out:
	parse_free(&dq, &st);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-jsv] [file]\n", getprogname());
	return(EXIT_FAILURE);
}
