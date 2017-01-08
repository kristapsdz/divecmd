/*	$Id$ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <expat.h>

#include "parser.h"

int verbose = 0;

enum	grapht {
	GRAPH_TEMP,
	GRAPH_DEPTH
};

/*
 * Used for computing averages.
 */
struct	avg {
	double		 accum;
	size_t		 sz;
};

/*
 * Drawing area size.
 * All values are absolute to the total screen size.
 */
struct	win {
	size_t		 rows; /* vertical space */
	size_t		 cols; /* horizontal space */
	size_t		 top; /* top offset */
	size_t		 left; /* left offset */
};

/*
 * A single graph type.
 */
struct	graph {
	size_t		*labels;
	size_t		*cols;
	size_t		 nsamps;
	size_t		 maxtime;
	double		 maxvalue;
	double		 minvalue;
};

static	const char *modes[] = {
	"Unknown-mode", /* MODE_NONE */
	"Free", /* MODE_FREEDIVE */
	"Gauge", /* MODE_GAUGE */
	"Open-circuit", /* MODE_OC */
	"Closed-circuit" /* MODE_CC */
};

static void
print_avgs(const struct avg *avgs, 
	const struct win *iwin, 
	double min, double max,
	size_t maxt, size_t lbuf, int dir)
{
	size_t		 x, y, lasty, ytics, xtics, t;
	double		 v;

	/* Make our y-axis and y-axis labels. */

	for (y = 0; y < iwin->rows; y++)
        	printf("\033[%zu;%zuH|", 
			iwin->top + y + 1, 
			iwin->left - 1 + 1);

       	printf("\033[%zu;%zuH\\", 
		iwin->top + y + 1, 
		iwin->left - 1 + 1);

	if (iwin->rows > 50)
		ytics = (iwin->rows - 1) / 8;
	else
		ytics = (iwin->rows - 1) / 4;

	for (y = 0; y < iwin->rows; y += ytics) {
		v = dir ?
			max - (max - min) * (double)y / iwin->rows :
			min + (max - min) * (double)y / iwin->rows;
        	printf("\033[%zu;%zuH-", 
			iwin->top + y + 1, 
			iwin->left - 1 + 1);
        	printf("\033[%zu;%zuH%*.1f", 
			iwin->top + y + 1, 
			iwin->left - lbuf + 1, (int)lbuf - 1, v);
	}

	/* Make sure we have a value at the maximum. */

	if ((iwin->rows > 50 && 0 != (iwin->rows - 1) % 8) ||
	    0 != (iwin->rows - 1) % 4) {
		v = dir ?
			max - (max - min) * (double)(iwin->rows - 1) / iwin->rows :
			min + (max - min) * (double)(iwin->rows - 1) / iwin->rows;
        	printf("\033[%zu;%zuH%*.1f", 
			iwin->top + (iwin->rows - 1) + 1, 
			iwin->left - lbuf + 1, (int)lbuf - 1,
			v);
        	printf("\033[%zu;%zuH-", 
			iwin->top + (iwin->rows - 1) + 1, 
			iwin->left - 1 + 1);
	}

	/* Now make the x-axis and x-axis label. */

	for (x = 0; x < iwin->cols; x++)
        	printf("\033[%zu;%zuH-", 
			iwin->top + iwin->rows + 1, 
			iwin->left + x + 1);

	if (iwin->cols > 100)
		xtics = (iwin->cols - 6) / 8;
	else
		xtics = (iwin->cols - 6) / 4;

	for (x = 0; x < iwin->cols; x += xtics) {
		t = maxt * ((double)x / iwin->cols);
        	printf("\033[%zu;%zuH|", 
			iwin->top + iwin->rows + 1, 
			iwin->left + x + 1);
        	printf("\033[%zu;%zuH%03zu:%02zu", 
			iwin->top + iwin->rows + 2, 
			iwin->left + x + 1,
			t / 60, t % 60);
	}

	for (lasty = x = 0; x < iwin->cols; x++) {
		if (0 == avgs[x].sz)
			continue;
		v = avgs[x].accum / (double)avgs[x].sz;
		y = dir ?
			(iwin->rows - 1) * ((max - v) / (max - min)) :
			(iwin->rows - 1) * ((v - min) / (max - min));

		/* Join from above and below. */

#if 0
		if (y > lasty)
			for (++lasty; lasty < y; lasty++) 
				printf("\033[1;36m\033[%zu;%zuH|",
					iwin->top + lasty + 1, 
					iwin->left + x + 1);
		else if (y < lasty)
			for (--lasty; lasty > y; lasty--)
				printf("\033[1;36m\033[%zu;%zuH|", 
					iwin->top + lasty + 1, 
					iwin->left + x + 1);
#endif

		/* Print our marker. */

	        printf("\033[1;36m\033[%zu;%zuH+", 
			iwin->top + y + 1, 
			iwin->left + x + 1);
	}

        printf("\033[0m");
}

static struct avg *
collect(const struct dive *d, size_t cols, 
	size_t maxt, enum grapht type)
{
	struct samp	*samp;
	double		 frac;
	size_t		 idx;
	struct avg	*avg = NULL;

	if (NULL == (avg = calloc(cols, sizeof(struct avg))))
		err(EXIT_FAILURE, NULL);

	TAILQ_FOREACH(samp, &d->samps, entries) {
		if (GRAPH_DEPTH == type &&
		    ! (SAMP_DEPTH & samp->flags))
			continue;
		if (GRAPH_TEMP == type &&
		    ! (SAMP_TEMP & samp->flags))
			continue;

		frac = (double)samp->time / maxt;
		idx = floor(frac * (cols - 1));
		assert(idx < cols);
		if (GRAPH_DEPTH == type)
			avg[idx].accum += samp->depth;
		else if (GRAPH_TEMP == type)
			avg[idx].accum += samp->temp;
		avg[idx].sz++;
	}

	return(avg);
}

/*
 * Print all graphs to the screen.
 * This first checks to see which graphs it should produce (trying for
 * all of them, if the data's there), preprocesses the data, then puts
 * everything onto the screen.
 */
static int
print_all(const struct dive *d, const struct winsize *ws)
{
	struct graph	 temp, depth;
	struct samp	*samp;
	int		 c, dtemp = 0, ddepth = 0;
	struct win	 win, iwin;
	size_t		 lbuf, maxt, titlesz;
	struct avg	*avg;
	char		*title;

	memset(&temp, 0, sizeof(struct graph));
	memset(&depth, 0, sizeof(struct graph));

	depth.minvalue = temp.minvalue = DBL_MAX;
	depth.maxvalue = temp.maxvalue = -DBL_MAX;

	/*
	 * First, establish whether we should do any graphing at all for
	 * the temperature or depth.
	 * Requirements are (1) at least 2 samples and (2) a non-zero
	 * difference between maximum and minimum.
	 */

	TAILQ_FOREACH(samp, &d->samps, entries) {
		if (SAMP_DEPTH & samp->flags) {
			depth.nsamps++;
			if (samp->time > depth.maxtime)
				depth.maxtime = samp->time;
			if (samp->depth > depth.maxvalue)
				depth.maxvalue = samp->depth;
			if (samp->depth < depth.minvalue)
				depth.minvalue = samp->depth;
		}
		if (SAMP_TEMP & samp->flags) {
			temp.nsamps++;
			if (samp->time > temp.maxtime)
				temp.maxtime = samp->time;
			if (samp->temp > temp.maxvalue)
				temp.maxvalue = samp->temp;
			if (samp->temp < temp.minvalue)
				temp.minvalue = samp->temp;
		}
	}

	if (temp.nsamps >= 2 && 
	    fabs(temp.maxvalue - temp.minvalue) > FLT_EPSILON)
		dtemp = 1;
	if (depth.nsamps >= 2 && 
	    fabs(depth.maxvalue - depth.minvalue) > FLT_EPSILON)
		ddepth = 1;

	if (0 == dtemp && 0 == ddepth) {
		warnx("nothing to graph");
		return(0);
	}

	/* Get the maximum time. */

	maxt = depth.maxtime;
	if (temp.maxtime > maxt)
		maxt = temp.maxtime;

	c = NULL != d->date && NULL != d->time ?
		asprintf(&title, "%s Dive #%zu on %s, %s",
			modes[d->mode], d->num, d->date, d->time) :
		asprintf(&title, "%s Dive #%zu",
			modes[d->mode], d->num);

	if (c < 0)
		err(EXIT_FAILURE, NULL);

	/* 
	 * Include a margin: 4 vertical (one at the top, two on the
	 * bottom, one being for the next prompt) and 2 horizontal (one
	 * on either side).
	 */

	win.rows = ws->ws_row - 4;
	win.cols = ws->ws_col - 2;
	win.top = 1;
	win.left = 1;

	/*
	 * We're going to graph at least one of the types.
	 * Begin by clearing our workspace.
	 */

	printf("\e[1;1H\e[2J");

	/* Title: centre, bold. */

	titlesz = strlen(title);
	if (titlesz >= win.cols) {
		title[win.cols - 2] = '\0';
		titlesz = strlen(title);
	}

       	printf("\033[%zu;%zuH\033[1m%s\033[0m", 
		win.top + 1, 
		win.left + ((win.cols - titlesz) / 2) + 1,
		title);

	lbuf = (dtemp && temp.maxvalue >= 100.0) || 
	       (ddepth && depth.maxvalue >= 100.0) ? 6 : 5;

	if (dtemp) {
		iwin = win;
		iwin.cols -= lbuf;
		iwin.left += lbuf;
		iwin.top += 1;
		if (ddepth)
			iwin.rows = (win.rows / 2) - 3;
		else
			iwin.rows -= 3;

		avg = collect(d, iwin.cols, maxt, GRAPH_TEMP);
		print_avgs(avg, &iwin, temp.minvalue, 
			temp.maxvalue, maxt, lbuf, 1);
		free(avg);
	}

	if (ddepth) {
		iwin = win;
		iwin.cols -= lbuf;
		iwin.left += lbuf;
		if (dtemp) {
			iwin.top = (win.rows / 2) + 2;
			iwin.rows = (win.rows / 2) - 3;
		} else {
			iwin.rows -= 3;
			iwin.top += 1;
		}

		avg = collect(d, iwin.cols, maxt, GRAPH_DEPTH);
		print_avgs(avg, &iwin, depth.minvalue, 
			depth.maxvalue, maxt, lbuf, 0);
		free(avg);
	}

	/* Put as at the bottom of the screen. */

        printf("\033[%d;%dH", ws->ws_row, 0);

	free(title);
	return(1);
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 0;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct winsize	 ws;

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

	if ( ! TAILQ_EMPTY(&dq))
		rc = print_all(TAILQ_FIRST(&dq), &ws);

	/* Free all memory from the dives. */

	parse_free(&dq);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-v] [file]\n", getprogname());
	return(EXIT_FAILURE);
}
