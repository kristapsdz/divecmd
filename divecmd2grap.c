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

#define	COL_MAX 5

static	const char *cols[COL_MAX] = {
	"dodgerblue2",
	"darkorange",
	"mediumorchid",
	"magenta4",
	"limegreen",
};

/*
 * Print derivatives, aka velocity.
 */
static int derivs = 0;

static int
print_all(const struct diveq *dq, const struct divestat *st)
{
	struct dive	*d, *dp;
	struct samp	*s;
	size_t		 i = 0, j;
	time_t		 t, lastt = 0;
	size_t		 maxtime = 0, maxrtime = 0, points = 0, rest;
	double		 maxdepth = 0.0, lastdepth, x, y;
	double		 height = 3.8, width = 5.4;
	int		 free = 0;
	struct dgroup 	*dg;

	assert( ! TAILQ_EMPTY(dq));

	/*
	 * For aggregate and the resting modes, we need our date-time to
	 * compute relative positions of dives with respect to each
	 * other.
	 * Thus, make sure that a date-time exists for all dives.
	 */

	if (MODE_AGGREGATE == mode ||
	    MODE_RESTING == mode ||
	    MODE_RESTING_SCATTER == mode) 
		TAILQ_FOREACH(d, dq, entries)
			if (0 == d->datetime) {
				warnx("date and time required");
				return(0);
			}

	if (MODE_AGGREGATE == mode)
		TAILQ_FOREACH(d, dq, entries) {
			t = (d->datetime + d->maxtime) - 
				d->group->mintime;
			assert(t >= 0);
			if ((size_t)t > maxtime)
				maxtime = t;
		}

	if (MODE_SUMMARY == mode ||
	    MODE_SCATTER == mode ||
	    MODE_RESTING == mode ||
	    MODE_STACK == mode ||
	    MODE_RESTING_SCATTER == mode)
		TAILQ_FOREACH(d, dq, entries) {
			free += MODE_FREEDIVE == d->mode;
			dp = TAILQ_NEXT(d, entries);
			if (d->maxtime > maxtime)
				maxtime = d->maxtime;
			if (d->maxdepth > maxdepth)
				maxdepth = d->maxdepth;
			points++;
		}

	if (MODE_RESTING == mode ||
	    MODE_RESTING_SCATTER == mode)
		for (i = 0; i < st->groupsz; i++) {
			dg = st->groups[i];
			TAILQ_FOREACH(d, &dg->dives, gentries) {
				dp = TAILQ_NEXT(d, gentries);
				rest = NULL == dp ? 0 :
					dp->datetime - 
					(d->datetime + d->maxtime);
				if (rest > maxrtime)
					maxrtime = rest;
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

	/* 
	 * Free-diving mode: print minimum optimal surface time.
	 * To do so, "square" the matrix and draw for twice the rest to
	 * surface time.
	 */

	if (MODE_RESTING_SCATTER == mode && free)
		printf("line dashed 0.05 from 0,0 to %g,1\n",
			2.0 * (maxtime / (double)maxrtime));

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
		       "label left \"Depth (m)\" down %g left 0.3\n"
		       "copy thru {\n"
		       " \"\\(bu\" size +3 color $4 at $1,$3\n"
		       " line dashed 0.05 from $1,0 to $1,$3 color $4\n"
		       " circle at $1,$2 color $4\n"
		       " line from $1,0 to $1,$2 color $4\n"
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
		       " \"\\(bu\" size +3 color $5 at $1,$3\n"
		       " line dotted from $1,0 to $1,$3 color $5\n"
		       "%s"
		       " circle at $1,$2 color $5\n"
		       " line from $1,0 to $1,$2 color $5\n"
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
		printf("ticks left out at "
				"0.0 \"00:00\", "
				"0.25 \"%zu:%.02zu\", "
				"0.5 \"%zu:%.02zu\", "
				"0.75 \"%zu:%.02zu\", "
				"1.0 \"%zu:%.02zu\"\n"
		       "ticks bot out at "
				"0.0 \"00:00\", "
				"0.25 \"%zu:%.02zu\", "
				"0.5 \"%zu:%.02zu\", "
				"0.75 \"%zu:%.02zu\", "
				"1.0 \"%zu:%.02zu\"\n"
		       "label left \"Dive time (mm:ss)\" left 0.15\n"
		       "label bot \"Rest time (mm:ss)\"\n"
		       "grid right ticks off\n"
		       "grid top ticks off\n"
		       "coord y 0,1\n"
		       "coord x 0,1\n"
		       "copy thru {\n"
		       " \"\\(bu\" size+3 color $5 at $2,-$3\n"
		       "}\n",
		       (maxtime / 4) / 60, (maxtime / 4) % 60, 
		       (maxtime / 2) / 60, (maxtime / 2) % 60, 
		       (3 * maxtime / 4) / 60, 
		       (3 * maxtime / 4) % 60, 
		       maxtime / 60, maxtime % 60,
		       (maxrtime / 4) / 60, (maxrtime / 4) % 60, 
		       (maxrtime / 2) / 60, (maxrtime / 2) % 60, 
		       (3 * maxrtime / 4) / 60, 
		       (3 * maxrtime / 4) % 60, 
		       maxrtime / 60, maxrtime % 60);
	else if (MODE_SCATTER == mode) 
		printf("ticks bot out at "
				"0.0 \"00:00\", "
				"0.25 \"%zu:%.02zu\", "
				"0.5 \"%zu:%.02zu\", "
				"0.75 \"%zu:%.02zu\", "
				"1.0 \"%zu:%.02zu\"\n"
		       "label left \"Depth (m)\" left 0.15\n"
		       "label bot \"Time (mm:ss)\"\n"
		       "grid right ticks off\n"
		       "grid top ticks off\n"
		       "coord y 0,-%g\n"
		       "coord x 0,1\n"
		       "copy thru {\n"
		       " \"\\(bu\" size +3 color $4 at $2,$3\n"
		       "}\n",
		       (maxtime / 4) / 60, (maxtime / 4) % 60, 
		       (maxtime / 2) / 60, (maxtime / 2) % 60, 
		       (3 * maxtime / 4) / 60, 
		       (3 * maxtime / 4) % 60, 
		       maxtime / 60, maxtime % 60,
		       maxdepth);
	else 
		printf("ticks bot out at "
				"0.0 \"00:00\", "
				"0.25 \"%zu:%.02zu\", "
				"0.5 \"%zu:%.02zu\", "
				"0.75 \"%zu:%.02zu\", "
				"1.0 \"%zu:%.02zu\"\n"
		       "grid right ticks off\n"
		       "grid top ticks off\n"
		       "label left \"%s\" left 0.1\n"
		       "label bot \"Time (mm:ss)\"\n"
		       "new color \"%s\"\n",
		       (maxtime / 4) / 60, (maxtime / 4) % 60, 
		       (maxtime / 2) / 60, (maxtime / 2) % 60, 
		       (3 * maxtime / 4) / 60, 
		       (3 * maxtime / 4) % 60, 
		       maxtime / 60, maxtime % 60,
		       ! derivs ?  "Depth (m)" :
		       "Velocity (vertical m/s)",
		       cols[0]);

	/* Now for the data. */

	if (MODE_AGGREGATE == mode) {
		/*
		 * In aggregate mode, we iterate through each of the
		 * groups, then through all of the dives in those
		 * groups, because we connect between group lines.
		 * This is different from the other processing methods,
		 * were dives are independent.
		 */
		for (i = 0; i < st->groupsz; i++) {
			dg = st->groups[i];
			lastt = 0;
			TAILQ_FOREACH(d, &dg->dives, gentries) {
				lastdepth = 0.0;
				x = (d->datetime - dg->mintime) /
					(double)maxtime;
				printf("%g 0\n", x);
				TAILQ_FOREACH(s, &d->samps, entries) {
					t = s->time;
					t += d->datetime;
					t -= dg->mintime;
					x = t / (double)maxtime;
					y = (0 == derivs) ? -s->depth :
						(lastt == t) ? 0.0 :
						(lastdepth - s->depth) /
						(t - lastt);
					printf("%g %g\n", x, y);
					lastdepth = s->depth;
					lastt = t;
				}
				x = lastt / (double)maxtime;
				printf("%g 0\n", x);
			}
			if (i == st->groupsz - 1)
				continue;
			printf("new color \"%s\"\n", 
				cols[st->groups[i + 1]->id % COL_MAX]);
		}
		goto out;
	} 
	
	if (MODE_RESTING == mode || MODE_RESTING_SCATTER == mode) {
		/*
		 * In the resting modes, we need to be within our group
		 * because we look to the next dive, which must also be
		 * in our group.
		 */
		for (i = j = 0; i < st->groupsz; i++) {
			dg = st->groups[i];
			TAILQ_FOREACH(d, &dg->dives, gentries) {
				dp = TAILQ_NEXT(d, gentries);
				t = NULL == dp ? 0 :
					dp->datetime - 
					(d->datetime + d->maxtime);
				printf("%zu %g -%g %g \"%s\"\n", j++, 
					t / (double)maxrtime,
					d->maxtime / (double)maxtime,
					(d->maxtime * 2) / (double)maxrtime,
					cols[d->group->id % COL_MAX]);
			}
		}
		goto out;
	 }

	TAILQ_FOREACH(d, dq, entries) {
		/* 
		 * We have a bunch of modes that just print "summary"
		 * information and don't iterate over the dives
		 * themselves.
		 * Print those first.
		 */
		if (MODE_SUMMARY == mode) {
			printf("%zu %g -%g \"%s\"\n", i++, 
				(double)d->maxtime / maxtime, 
				d->maxdepth / maxdepth,
				cols[d->group->id % COL_MAX]);
			continue;
		} else if (MODE_SCATTER == mode) {
			printf("%zu %g -%g \"%s\"\n", i++, 
				d->maxtime / (double)maxtime, 
				d->maxdepth,
				cols[d->group->id % COL_MAX]);
			continue;
		}

		puts("0 0");
		lastdepth = 0.0;
		TAILQ_FOREACH(s, &d->samps, entries) {
			t = s->time;
			x = t / (double)maxtime;
			y = 0 == derivs ? -s->depth :
				lastt == t ? 0.0 :
				(lastdepth - s->depth) /
				(t - lastt);
			printf("%g %g\n", x, y);
			lastdepth = s->depth;
			lastt = t;
		}

		x = lastt / (double)maxtime;
		printf("%g 0\n", x);
		if (NULL != (dp = TAILQ_NEXT(d, entries)))
			printf("new color \"%s\"\n",
				cols[dp->group->id % COL_MAX]);
	}
out:
	puts(".G2");
	return(1);
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 1;
	enum group	 group = GROUP_NONE;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct divestat	 st;

	/* Pledge us early: only reading files. */

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif
	while (-1 != (c = getopt(argc, argv, "dm:s:v")))
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
		case ('s'):
			if (0 == strcasecmp(optarg, "date"))
				group = GROUP_DATE;
			else if (0 == strcasecmp(optarg, "none"))
				group = GROUP_NONE;
			else
				goto usage;
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
	    (MODE_AGGREGATE != mode && MODE_STACK != mode))
		warnx("-d: ignoring flag");

	parse_init(&p, &dq, &st, group);

	/* 
	 * Handle all files or stdin.
	 * File "-" is interpreted as stdin.
	 */

	if (0 == argc)
		rc = parse("-", p, &dq, &st);

	for (i = 0; i < (size_t)argc; i++)
		if ( ! (rc = parse(argv[i], p, &dq, &st)))
			break;

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

	rc = print_all(&dq, &st);
out:
	parse_free(&dq, &st);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-dv] "
		"[-m mode] "
		"[-s splitgroup] "
		"[file...]\n", getprogname());
	return(EXIT_FAILURE);
}
