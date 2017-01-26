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

int verbose = 0;

/*
 * Aggregate mode: print dives one after the other, starting from the
 * minimum time, with all dive lines joined.
 */
static int aggr = 0;

/*
 * Make sure to start and end each dive on the surface.
 */
static int topside = 0;

/*
 * Standalone.
 * Print a grap(1) document around the data.
 */
static int standalone = 0;

static int
print_all(const struct diveq *dq)
{
	struct dive	*d;
	struct samp	*s;
	time_t		 mintime = 0, t, lastt = 0;

	assert( ! TAILQ_EMPTY(dq));

	if (aggr) 
		TAILQ_FOREACH(d, dq, entries) {
			if (0 == d->datetime) {
				warnx("date and time required");
				return(0);
			}
			if (0 == mintime || d->datetime < mintime)
				mintime = d->datetime;
		}

	if (standalone) 
		puts(".G1\n"
		     "draw solid\n"
		     "frame invis ht 3.5 wid 5 left solid bot solid\n"
		     "label left \"Depth\" \"(metres)\" left 0.2\n"
		     "label bot \"Time (seconds)\"");

	TAILQ_FOREACH(d, dq, entries) {
		if (topside)
			printf("%lld 0\n", aggr ?
				d->datetime - mintime : 0);
		TAILQ_FOREACH(s, &d->samps, entries) {
			t = s->time;
			if (aggr) {
				t += d->datetime;
				t -= mintime;
			}
			if (SAMP_DEPTH & s->flags)
				printf("%lld -%g\n", t, s->depth);
			lastt = t;
		}
		if (topside)
			printf("%lld 0\n", lastt);
		if ( ! aggr && TAILQ_NEXT(d, entries)) 
			puts("new");
	}

	if (standalone)
		puts(".G2");

	return(1);
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 1;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct divestat	 st;

	/* Pledge us early: only reading files. */

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif
	
	memset(&st, 0, sizeof(struct divestat));

	while (-1 != (c = getopt(argc, argv, "astv")))
		switch (c) {
		case ('a'):
			aggr = 1;
			break;
		case ('s'):
			standalone = 1;
			break;
		case ('t'):
			topside = 1;
			break;
		case ('v'):
			verbose = 1;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	if (NULL == (p = XML_ParserCreate(NULL)))
		err(EXIT_FAILURE, NULL);

	TAILQ_INIT(&dq);

	/* 
	 * Handle all files or stdin.
	 * File "-" is interpreted as stdin.
	 */

	if (0 == argc)
		rc = parse("-", p, &dq, &st);

	for (i = 0; i < (size_t)argc; i++)
		if ( ! (rc = parse(argv[i], p, &dq, &st)))
			break;

	XML_ParserFree(p);

	if ( ! rc)
		goto out;

	if (TAILQ_EMPTY(&dq)) {
		warnx("no dives to display");
		goto out;
	}

	/* 
	 * Parsing is finished.
	 * Narrow the pledge to just stdio.
	 * From now on, we process and paint.
	 */

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	rc = print_all(&dq);
out:
	parse_free(&dq);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-astv] [file]\n", getprogname());
	return(EXIT_FAILURE);
}