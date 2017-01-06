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
#include <string.h>

#include <expat.h>

#include "parser.h"

int verbose = 0;

struct	win {
	size_t		 rows;
	size_t		 cols;
	size_t		 top;
	size_t		 left;
};

static	const char *modes[] = {
	"Unknown-mode", /* MODE_NONE */
	"Free", /* MODE_FREEDIVE */
	"Gauge", /* MODE_GAUGE */
	"Open-circuit", /* MODE_OC */
	"Closed-circuit" /* MODE_CC */
};

static void
print_depths(const struct dive *d, const struct win *win)
{
	struct samp	*samp;
	size_t		 x, nsamps, cursamps, y, lasty, lastt, 
			 lbuf, titlesz;
	double		 accum, incr, bucket, lastavg, avg;
	size_t		*cols = NULL, *labels = NULL;
	struct win	 iwin;
	char		*title;
	int		 c;

	/* Get the number of depth-related samples. */

	nsamps = 0;
	TAILQ_FOREACH(samp, &d->samps, entries)
		if (SAMP_DEPTH & samp->flags)
			nsamps++;

	if (0 == nsamps)
		return;


	/* Create the "inner window" plotting area. */

	iwin = *win;

	/* 
	 * Now make room for our border and label.
	 * The y-axis gets the number of metres, up to xxx.y, then the
	 * border.
	 * The x-axis gets the border, then the labels.
	 */

	lbuf = d->maxdepth >= 100.0 ? 6 : 5;

	iwin.cols -= lbuf;
	iwin.left += lbuf;
	iwin.top += 1;
	iwin.rows -= 3;

	/* Initialise our data buckets. */

	cols = calloc(win->cols, sizeof(size_t));
	labels = calloc(win->cols, sizeof(size_t));
	if (NULL != d->date && NULL != d->time)
		c = asprintf(&title, "%s Dive #%zu on %s %s", 
			modes[d->mode], d->num, d->date, d->time);
	else
		c = asprintf(&title, "%s Dive #%zu", modes[d->mode], d->num);

	if (NULL == cols || NULL == labels || c < 0)
		err(EXIT_FAILURE, NULL);

	/*
	 * Compute how many samples should go in each column.
	 * This is floating-point so that we have a smooth transition
	 * regardless the imbalance.
	 * It goes like this:
	 *
	 *   |x0 x1 x2||y0 y1 y2||z0 z1|| <- samples w/depth
	 *   +---x----++---y----++--z--+  <- buckets
	 *
	 * The number of x,y,z we travel depends on how the buckets
	 * divide into the samples.
	 * This also works if the number of buckets is greater than the
	 * number of samples, in which we case we spread them out.
	 *
	 * FIXME: we might have staggered sampling time, which will make
	 * the data not quite representative.
	 */

	incr = (double)iwin.cols / nsamps;
	x = lastt = cursamps = 0;
	accum = lastavg = bucket = 0.0;

	TAILQ_FOREACH(samp, &d->samps, entries) {
		lastt = samp->time;

		/* Skip over non-depth samples. */

		if ( ! (SAMP_DEPTH & samp->flags))
			continue;

		/*
		 * Accumulate samples until our accumulated number of
		 * buckets is greater than the next output column.
		 */

		accum += incr;
		bucket += samp->depth;
		cursamps++;

		if ((x + 1) > (size_t)floor(accum)) 
			continue;

		/* 
		 * Now we have enough data for at least one bucket. 
		 * Compute the y-value appropriately. */

		avg = 0 == cursamps ? lastavg : bucket / cursamps;
		lastavg = avg;
		y = (size_t)((iwin.rows - 1) * (avg / d->maxdepth));
		assert(y < iwin.rows);

		/* Skip to the next x-value. */

		for ( ; x < (size_t)floor(accum); x++) {
			assert(x < iwin.cols);
			labels[x] = lastt;
			cols[x] = y;
		}

		/* Reset our bucket accumulation. */

		bucket = 0.0;
		cursamps = 0;
	}

	assert(NULL == samp);

	/* Off-by-one: rounding errors. */

	if (x < iwin.cols) {
		assert(x == iwin.cols - 1);
		cols[x] = cols[x - 1];
		labels[x] = labels[x - 1];
	}

	/* Title: centre, bold. */

	titlesz = strlen(title);
	if (titlesz >= win->cols) {
		title[win->cols - 2] = '\0';
		titlesz = strlen(title);
	}

       	printf("\033[%zu;%zuH\033[1m%s\033[0m", 
		iwin.top - 1 + 1, 
		win->left + ((win->cols - titlesz) / 2) + 1,
		title);

	/* Make our y-axis and y-axis labels. */

	for (y = 0; y < iwin.rows; y++)
        	printf("\033[%zu;%zuH|", 
			iwin.top + y + 1, 
			iwin.left - 1 + 1);

       	printf("\033[%zu;%zuH\\", 
		iwin.top + y + 1, 
		iwin.left - 1 + 1);

	for (y = 0; y < iwin.rows; y += 10) {
        	printf("\033[%zu;%zuH-", 
			iwin.top + y + 1, 
			iwin.left - 1 + 1);
        	printf("\033[%zu;%zuH%5.1f", 
			iwin.top + y + 1, 
			iwin.left - lbuf + 1,
			d->maxdepth * (double)y / iwin.rows);
	}

	/* Now make the x-axis and x-axis label. */

	for (x = 0; x < iwin.cols; x++)
        	printf("\033[%zu;%zuH-", 
			iwin.top + iwin.rows + 1, 
			iwin.left + x + 1);

	for (x = 0; x < iwin.cols; x += 10) {
        	printf("\033[%zu;%zuH|", 
			iwin.top + iwin.rows + 1, 
			iwin.left + x + 1);
        	printf("\033[%zu;%zuH%03zu:%02zu", 
			iwin.top + iwin.rows + 2, 
			iwin.left + x + 1,
			labels[x] / 60, labels[x] % 60);
	}

	for (lasty = x = 0; x < iwin.cols; x++) {
		y = (size_t)cols[x];

		/* Join from above and below. */

		if (y > lasty)
			for (++lasty; lasty < y; lasty++) 
				printf("\033[1;36m\033[%zu;%zuH|",
					iwin.top + lasty + 1, 
					iwin.left + x + 1);
		else if (y < lasty)
			for (--lasty; lasty > y; lasty--)
				printf("\033[1;36m\033[%zu;%zuH|", 
					iwin.top + lasty + 1, 
					iwin.left + x + 1);

		/* Print our marker. */

	        printf("\033[1;36m\033[%zu;%zuH+", 
			iwin.top + y + 1, 
			iwin.left + x + 1);
	}

	free(cols);
	free(labels);
	free(title);
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 0;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct winsize	 ws;
	struct win	 win;

	/* Pledge us early: only reading files. */

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (c = getopt(argc, argv, "f:v")))
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

	if (TAILQ_EMPTY(&dq)) {
		warnx("no dives to display");
		return(EXIT_FAILURE);
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

	/*
	 * Initialise screen real estate.
	 * By default, pretend we're a 80x25, which will also be our
	 * minimum size.
	 * Otherwise, use the terminal's reported dimensions.
	 */

	if (-1 == ioctl(0, TIOCGWINSZ, &ws)) {
		warnx("TIOCGWINSZ");
		ws.ws_row = 25;
		ws.ws_col = 80;
	}

	if (ws.ws_row < 25)
		ws.ws_row = 25;
	if (ws.ws_col < 80)
		ws.ws_col = 80;

	/* Clear the screen. */

	printf("\e[1;1H\e[2J");

	/* 
	 * Include a margin: 3 vertical (one at the top, two on the
	 * bottom, one being for the next prompt) and 2 horizontal (one
	 * on either side).
	 */

	win.rows = ws.ws_row - 3;
	win.cols = ws.ws_col - 2;
	win.top = 1;
	win.left = 1;

	if ( ! TAILQ_EMPTY(&dq))
		print_depths(TAILQ_FIRST(&dq), &win);

	/* Free all memory from the dives. */

	parse_free(&dq);

	/* Put as at the bottom of the screen. */

        printf("\033[0m\033[%d;%dH", ws.ws_row, 0);

	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-v] [file]\n", getprogname());
	return(EXIT_FAILURE);
}
