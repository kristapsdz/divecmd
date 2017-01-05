/*	$Id$ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>,
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
#include <sys/ioctl.h>
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <expat.h>

#include "parser.h"

int verbose = 0;

static void
print_dive(const struct dive *d)
{
	struct samp	*samp;
	struct winsize	 ws;
	size_t		 x, nsamps, cursamps, y;
	double		 accum, incr, bucket, lastavg, avg;
	int		 lasty;
	int		*cols = NULL;

	/* Get the number of depth-related samples. */

	nsamps = 0;
	TAILQ_FOREACH(samp, &d->samps, entries)
		if (SAMP_DEPTH & samp->flags)
			nsamps++;
	if (0 == nsamps)
		return;

	/*
	 * Initialise screen real estate.
	 * By default, pretend we're a 80x25.
	 * Otherwise, use the terminal's reported dimensions.
	 */

	if (-1 == ioctl(0, TIOCGWINSZ, &ws)) {
		ws.ws_row = 25;
		ws.ws_col = 80;
	}

	if (ws.ws_row == 0)
		ws.ws_row = 25;
	if (ws.ws_col == 0)
		ws.ws_col = 80;

	cols = calloc(ws.ws_col, sizeof(int));
	if (NULL == cols)
		err(EXIT_FAILURE, NULL);
	for (x = 0; x < (size_t)ws.ws_col; x++)
		cols[x] = -1;

	/*
	 * Compute how many samples should go in each column.
	 * This is floating-point so that we have a smooth transition
	 * regardless the imbalance.
	 */

	incr = (double)(ws.ws_col - 1) / nsamps;
	x = 0;
	accum = lastavg = 0.0;

	TAILQ_FOREACH(samp, &d->samps, entries) {
		if ( ! (SAMP_DEPTH & samp->flags))
			continue;

		accum += incr;
		bucket = 0.0;
		cursamps = 0;

		/* Walk columns, averaging samples. */

		while (x < (size_t)floor(accum)) {
			bucket += samp->depth;
			cursamps++;
			x++;
		}
		avg = 0 == cursamps ? lastavg : bucket / cursamps;
		lastavg = avg;

		/* Now compute our y-position. */

		y = (size_t)((ws.ws_row - 1) * (avg / d->maxdepth));

		/* Draw on the screen. */

		assert(x < ws.ws_col);
		assert(y < ws.ws_row);
		cols[x] = y;
	}

	/* Clear the screen. */

	printf("\e[1;1H\e[2J");

	lasty = 0;
	for (x = 0; x < (size_t)ws.ws_col; x++) {
		/* TODO */
		if (-1 == cols[x])
			continue;

		/* Join lines. */

		if (cols[x] > lasty) {
			lasty++;
			while (lasty < cols[x]) {
				printf("\033[1;36m\033[%d;%zuH%s", lasty, x, "|");
				lasty++;
			}
		} else if (cols[x] < lasty) {
			lasty--;
			while (lasty > cols[x]) {
				printf("\033[1;36m\033[%d;%zuH%s", lasty, x, "|");
				lasty--;
			}
		}

	        printf("\033[1;36m\033[%d;%zuH%s", cols[x], x, "_");
	}

	/* Put as at the bottom of the screen. */

        printf("\033[0m\033[%d;%dH", ws.ws_row, 0);
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 0;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct dive	*d;
	struct samp	*samp;

	/* Pledge us early: only reading files. */

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (c = getopt(argc, argv, "v")))
		switch (c) {
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
		rc = parse("-", p, &dq);
	for (i = 0; i < (size_t)argc; i++)
		if ( ! (rc = parse(argv[i], p, &dq)))
			break;

	XML_ParserFree(p);

	/* 
	 * Parsing is finished.
	 * Narrow the pledge to just stdio.
	 * From now on, we process and paint.
	 */

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	if ( ! TAILQ_EMPTY(&dq))
		print_dive(TAILQ_FIRST(&dq));

	/* Free all memory from the dives. */

	while ( ! TAILQ_EMPTY(&dq)) {
		d = TAILQ_FIRST(&dq);
		TAILQ_REMOVE(&dq, d, entries);
		while ( ! TAILQ_EMPTY(&d->samps)) {
			samp = TAILQ_FIRST(&d->samps);
			TAILQ_REMOVE(&d->samps, samp, entries);
			free(samp);
		}
		free(d);
	}

	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-v] [file]\n", getprogname());
	return(EXIT_FAILURE);
}
