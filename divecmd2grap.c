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
	MODE_SCATTER,
	MODE_SUMMARY,
	MODE_AGGREGATE,
	MODE_RESTING,
	MODE_RESTING_SCATTER,
	MODE_STACK
};

int verbose = 0;

static	enum pmode mode = MODE_STACK;

/*
 * Make sure to start and end each dive on the surface.
 */
static int topside = 0;

/*
 * Print derivatives, aka velocity.
 */
static int derivs = 0;

static int
print_all(const struct diveq *dq)
{
	struct dive	*d, *dp;
	struct samp	*s;
	size_t		 i = 0;
	time_t		 mintime = 0, t, lastt = 0;
	size_t		 maxtime = 0, maxrtime = 0, points = 0, rest;
	double		 maxdepth = 0.0, lastdepth;
	double		 height = 3.8, width = 5.4;
	int		 free = 0;

	assert( ! TAILQ_EMPTY(dq));

	/*
	 * For aggregate mode, we need to put dives end-to-end, so we
	 * need the datetime for each dive.
	 */

	if (MODE_AGGREGATE == mode ||
	    MODE_RESTING == mode ||
	    MODE_RESTING_SCATTER == mode) 
		TAILQ_FOREACH(d, dq, entries) {
			if (0 == d->datetime) {
				warnx("date and time required");
				return(0);
			}
			if (0 == mintime || d->datetime < mintime)
				mintime = d->datetime;
		}

	if (MODE_SUMMARY == mode ||
	    MODE_SCATTER == mode ||
	    MODE_RESTING == mode ||
	    MODE_RESTING_SCATTER == mode) {
		TAILQ_FOREACH(d, dq, entries) {
			free += MODE_FREEDIVE == d->mode;
			dp = TAILQ_NEXT(d, entries);
			if (d->maxtime > maxtime)
				maxtime = d->maxtime;
			if (d->maxdepth > maxdepth)
				maxdepth = d->maxdepth;
			rest = NULL == dp ? 0 :
				dp->datetime - 
				(d->datetime + d->maxtime);
			if (rest > maxrtime)
				maxrtime = rest;
			points++;
		}
	}

	/*
	 * Print our labels, axes, and so on.
	 * This depends primarily upon our mode.
	 */

	printf(".G1\n"
	       "draw solid\n"
	       "frame invis ht %g wid %g left solid bot solid\n",
	       height, width);

	/* Free-diving mode: print minimum optimal surface time. */

	if (MODE_RESTING_SCATTER == mode && free)
		printf("line dotted from 0,0 to %zu,%zu\n",
		       maxtime * 2, maxtime);

	if (MODE_SUMMARY == mode)
		printf("ticks left out at "
				"-1.0 \"-%.2f\", "
				"-0.75 \"-%.2f\", "
				"-0.5 \"-%.2f\", "
				"-0.25 \"-%.2f\", "
				"0.0, "
				"0.25 \"%zu:%.02zu\", "
				"0.5 \"%zu:%.02zu\", "
				"0.75 \"%zu:%.02zu\", "
				"1.0 \"%zu:%.02zu\"\n"
		       "grid left from -1 to 1 by 0.25 \"\"\n"
		       "ticks bot off\n"
		       "line from 0,0.0 to %zu,0.0\n"
		       "label right \"Time (mm:ss)\" up %g left 0.2\n"
		       "label left \"Depth (metres)\" down %g left 0.3\n"
		       "copy thru {\n"
		       " \"\\(bu\" size +3 at $1,$3\n"
		       " line dashed 0.05 from $1,0 to $1,$3\n"
		       " circle at $1,$2\n"
		       " line from $1,0 to $1,$2\n"
		       "}\n",
		       maxdepth, 
		       0.75 * maxdepth,
		       0.5 * maxdepth,
		       0.25 * maxdepth,
		       (maxtime / 4) / 60, 
		       (maxtime / 4) % 60, 
		       (maxtime / 2) / 60, 
		       (maxtime / 2) % 60, 
		       (3 * maxtime / 4) / 60, 
		       (3 * maxtime / 4) % 60, 
		       maxtime / 60, 
		       maxtime % 60, points - 1,
		       0.25 * height, 0.25 * height);
	else if (MODE_RESTING == mode)
		printf("ticks left out at "
				"-1.0 \"%zu:%.02zu\", "
				"-0.75 \"%zu:%.02zu\", "
				"-0.5 \"%zu:%.02zu\", "
				"-0.25 \"%zu:%.02zu\", "
				"0.0, "
				"0.25 \"%zu:%.02zu\", "
				"0.5 \"%zu:%.02zu\", "
				"0.75 \"%zu:%.02zu\", "
				"1.0 \"%zu:%.02zu\"\n"
		       "grid left from -1 to 1 by 0.25 \"\"\n"
		       "ticks bot off\n"
		       "line from 0,0.0 to %zu,0.0\n"
		       "label right \"Rest time (mm:ss)\" up %g left 0.2\n"
		       "label left \"Dive time (mm:ss)\" down %g left 0.3\n"
		       "copy thru {\n"
		       " \"\\(bu\" size +3 at $1,$3\n"
		       " line dotted from $1,0 to $1,$3\n"
		       "%s"
		       " circle at $1,$2\n"
		       " line from $1,0 to $1,$2\n"
		       "}\n",
		       maxtime / 60, maxtime % 60, 
		       (3 * maxtime / 4) / 60, 
		       (3 * maxtime / 4) % 60, 
		       (maxtime / 2) / 60, (maxtime / 2) % 60, 
		       (maxtime / 4) / 60, (maxtime / 4) % 60, 
		       (maxrtime / 4) / 60, (maxrtime / 4) % 60, 
		       (maxrtime / 2) / 60, (maxrtime / 2) % 60, 
		       (3 * maxrtime / 4) / 60, 
		       (3 * maxrtime / 4) % 60, 
		       maxrtime / 60, maxrtime % 60, 
		       points - 1, 0.25 * height, 0.25 * height,
		       free ? " \"\\(en\" at $1,$4\n" : "");
	else if (MODE_RESTING_SCATTER == mode) 
		printf("label left \"Dive time (seconds)\" left 0.15\n"
		       "label bot \"Rest time (seconds)\"\n"
		       "coord y 0,%zu\n"
		       "coord x 0,%zu\n"
		       "copy thru { circle at $2,$3 }\n",
		       maxtime, maxrtime);
	else if (MODE_SCATTER == mode) 
		puts("label left \"Depth (metres)\" left 0.15\n"
		     "label bot \"Time (seconds)\"\n"
		     "copy thru { circle at $2,$3 }");
	else if ( ! derivs)
		puts("label left \"Depth (metres)\"\n"
		     "label bot \"Time (seconds)\"");
	else 
		puts("label left \"Velocity "
			"(vertical metres/second)\"\n"
		     "label bot \"Time (seconds)\"");

	/* Now for the data... */

	TAILQ_FOREACH(d, dq, entries) {
		lastdepth = 0.0;
		dp = TAILQ_NEXT(d, entries);

		/* 
		 * Summary mode is just the maxima of the dive, so print
		 * that and continue.
		 */

		if (MODE_SUMMARY == mode) {
			printf("%zu %g -%g\n", i++, 
				(double)d->maxtime / maxtime, 
				d->maxdepth / maxdepth);
			continue;
		} else if (MODE_RESTING == mode) {
			printf("%zu %g -%g %g\n", i++, 
				NULL == dp ? 0 : 
				(dp->datetime - 
				(d->datetime + d->maxtime)) / (double)maxrtime,
				d->maxtime / (double)maxtime,
				(d->maxtime * 2) / (double)maxrtime);
			dp = d;
			continue;
		} else if (MODE_RESTING_SCATTER == mode) {
			printf("%zu %lld %zu\n", i++, 
				NULL == dp ? 0 : 
				dp->datetime - 
				(d->datetime + d->maxtime),
				d->maxtime);
			dp = d;
			continue;
		} else if (MODE_SCATTER == mode) {
			printf("%zu %zu -%g\n", i++, 
				d->maxtime, d->maxdepth);
			continue;
		}

		/* 
		 * Topside: the dive computer isn't going to record an
		 * initial (zero) state, so print it now.
		 */

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

			/* Print depths or velocity. */

			if (SAMP_DEPTH & s->flags && derivs) {
				printf("%lld %g\n", t, 
					lastt == t ? 0.0 :
					(lastdepth - s->depth) /
					(t - lastt));
				lastdepth = s->depth;
				lastt = t;
			} else if (SAMP_DEPTH & s->flags) {
				printf("%lld -%g\n", t, s->depth);
				lastt = t;
			}
		}

		if (topside)
			printf("%lld 0\n", lastt);

		if (MODE_AGGREGATE != mode && 
		    TAILQ_NEXT(d, entries)) 
			puts("new");
	}

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

	while (-1 != (c = getopt(argc, argv, "dm:tv")))
		switch (c) {
		case ('d'):
			derivs = 1;
			break;
		case ('m'):
			if (0 == strcasecmp(optarg, "stack"))
				mode = MODE_STACK;
			else if (0 == strcasecmp(optarg, "aggr"))
				mode = MODE_AGGREGATE;
			else if (0 == strcasecmp(optarg, "summary"))
				mode = MODE_SUMMARY;
			else if (0 == strcasecmp(optarg, "rest"))
				mode = MODE_RESTING;
			else if (0 == strcasecmp(optarg, "restscatter"))
				mode = MODE_RESTING_SCATTER;
			else if (0 == strcasecmp(optarg, "scatter"))
				mode = MODE_SCATTER;
			else
				goto usage;
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

	if (derivs && 
	    (MODE_SCATTER == mode || MODE_SUMMARY == mode))
		warnx("-d: ignoring flag");
	if (topside && 
	    (MODE_SCATTER == mode || MODE_SUMMARY == mode))
		warnx("-t: ignoring flag");

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
	fprintf(stderr, "usage: %s [-dtv] "
		"[-m mode] [file...]\n", getprogname());
	return(EXIT_FAILURE);
}
