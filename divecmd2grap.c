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
	MODE_SUMMARY,
	MODE_AGGREGATE,
	MODE_STACK
};

int verbose = 0;

static	enum pmode mode = MODE_STACK;

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
	size_t		 i = 0;
	time_t		 mintime = 0, t, lastt = 0;
	size_t		 maxtime = 0, points = 0;
	double		 maxdepth = 0.0;
	double		 height = 3.0, width = 5.0;

	assert( ! TAILQ_EMPTY(dq));

	if (MODE_AGGREGATE == mode) 
		TAILQ_FOREACH(d, dq, entries) {
			if (0 == d->datetime) {
				warnx("date and time required");
				return(0);
			}
			if (0 == mintime || d->datetime < mintime)
				mintime = d->datetime;
		}

	if (MODE_SUMMARY == mode) 
		TAILQ_FOREACH(d, dq, entries) {
			if (d->maxtime > maxtime)
				maxtime = d->maxtime;
			if (d->maxdepth > maxdepth)
				maxdepth = d->maxdepth;
			points++;
		}

	if (standalone)
		printf(".G1\n"
		       "draw solid\n"
		       "frame invis ht %g wid %g left solid bot solid\n",
		       height, width);
	if (standalone && MODE_SUMMARY == mode)
		printf("ticks left out at "
				"-1.0 \"-%.2f\", "
				"-0.5 \"-%.2f\", 0.0, "
				"0.5 \"%zu:%.02zu\", "
				"1.0 \"%zu:%.02zu\"\n"
		       "ticks bot off\n"
		       "line from 0,0.0 to %zu,0.0\n"
		       "label right \"Time\" \"(mm:ss)\" up %g left 0.5\n"
		       "label left \"Depth\" \"(metres)\" down %g left 0.5\n"
		       "copy thru {\n"
		       " \"\\(bu\" size +3 at $1,$3\n"
		       " line dotted from $1,0 to $1,$3\n"
		       " line from $1,0 to $1,$2\n"
		       " circle at $1,$2\n"
		       "}\n",
		       maxdepth, 0.5 * maxdepth,
		       (maxtime / 2) / 60, 
		       (maxtime / 2) % 60, 
		       maxtime / 60, 
		       maxtime % 60, points,
		       0.25 * height, 0.25 * height);
	else if (standalone)
		puts("label left \"Depth\" \"(metres)\" left 0.2\n"
		     "label bot \"Time (seconds)\"");

	TAILQ_FOREACH(d, dq, entries) {
		if (MODE_SUMMARY == mode) {
			printf("%zu %g -%g\n", i++, 
				(double)d->maxtime / maxtime, 
				d->maxdepth / maxdepth);
			continue;
		}

		if (topside)
			printf("%lld 0\n", 
				MODE_AGGREGATE == mode ?
				d->datetime - mintime : 0);
		TAILQ_FOREACH(s, &d->samps, entries) {
			t = s->time;
			if (MODE_AGGREGATE == mode) {
				t += d->datetime;
				t -= mintime;
			}
			if (SAMP_DEPTH & s->flags)
				printf("%lld -%g\n", t, s->depth);
			lastt = t;
		}
		if (topside)
			printf("%lld 0\n", lastt);
		if (MODE_AGGREGATE != mode && 
		    TAILQ_NEXT(d, entries)) 
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

	while (-1 != (c = getopt(argc, argv, "m:stv")))
		switch (c) {
		case ('m'):
			if (0 == strcasecmp(optarg, "stack"))
				mode = MODE_STACK;
			else if (0 == strcasecmp(optarg, "aggr"))
				mode = MODE_AGGREGATE;
			else if (0 == strcasecmp(optarg, "summary"))
				mode = MODE_SUMMARY;
			else
				goto usage;
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
	fprintf(stderr, "usage: %s [-stv] "
		"[-m mode] [file...]\n", getprogname());
	return(EXIT_FAILURE);
}
