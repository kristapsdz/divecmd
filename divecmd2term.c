/*	$Id$ */
/*
 * Copyright (c) 2016--2018 Kristaps Dzonsons <kristaps@bsd.lv>
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

#include <sys/ioctl.h>
#include <sys/queue.h>

#include <assert.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#include <expat.h>

#include "parser.h"

/*
 * Print parse warnings.
 */
int verbose = 0;

/*
 * Disable UTF-8 and colours output mode.
 */
static int dumb = 0;

/*
 * Print the legend (dive titles).
 */
static int showlegend = 0;

/*
 * Print temperatures, if found.
 */
static int showtemp = 0;

/*
 * Aggregate dives.
 * Assume that this is a sequence (say of free-dives) where we should
 * graph multiple entries alongside each other.
 */
static int aggr = 0;

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
print_legend(const struct diveq *dq, const struct win *win)
{
	const struct dive *d;
	int		   c;
	size_t		   i, titlesz;
	char		  *title;
	char		   buf[128];
	
	i = 0;
	TAILQ_FOREACH(d, dq, entries) {
		if (d->datetime) {
			ctime_r(&d->datetime, buf);
			buf[strlen(buf) - 1] = '\0';
		}
		c = 0 != d->datetime ?
			asprintf(&title, "%s Dive #%zu on %s",
				modes[d->mode], d->num, buf) :
			asprintf(&title, "%s Dive #%zu",
				modes[d->mode], d->num);
		if (c < 0)
			err(EXIT_FAILURE, NULL);

		/* Title: centre, bold.  Room for legend. */

		titlesz = strlen(title) + 2;
		if (titlesz >= win->cols) {
			title[win->cols - 4] = '\0';
			titlesz = strlen(title) + 2;
		}

		if ( ! dumb)
			printf("\033[%zu;%zuH\033[1m%s \033[3%zum%lc\033[0m", 
				win->top + i + 1, 
				win->left + ((win->cols - titlesz) / 2) + 1,
				title, i + 1, L'\x2022');
		else
			printf("\033[%zu;%zuH\033[1m%s \033[3%zum+\033[0m", 
				win->top + i + 1, 
				win->left + ((win->cols - titlesz) / 2) + 1,
				title, i + 1);

		free(title);
		i++;
	}
}

/*
 * Print a set of averages.
 * Accepts the averages, "avgs", and the window in which to print,
 * "iwin".
 * The "min" and "max" are the total range of all points, and "mint" and
 * "maxt" is the x-axis extrema.
 * The "lbuf" tells us the left-hand buffer (for formatting axes.)
 * Finally, "dir" tells us whether the direction to show the y-axis
 * values, upward (1) or downward (0).
 */
static void
print_avgs(const struct avg *const *avgs, size_t avgsz,
	const struct win *iwin, double min, double max, 
	time_t mint, time_t maxt, size_t lbuf, int dir)
{
	size_t		 i, x, y, t, ytics, xtics, datarows;
	double		 v;

	assert(iwin->rows >= 6);
	datarows = iwin->rows - 2;

	/* 
	 * Draw the y-axis border down to the x-axis.
	 * The x-axis is one shy of the bottom, which has the label.
	 * Note that all ANSI escapes are from 1, not 0.
	 */

	for (y = 0; y < datarows; y++)
		if ( ! dumb)
			printf("\033[%zu;%zuH%lc", 
				iwin->top + y + 1, 
				iwin->left - 1 + 1, L'\x2502');
		else
			printf("\033[%zu;%zuH|", 
				iwin->top + y + 1, 
				iwin->left - 1 + 1);

	/* We have ticks enough for all but the x-axis line. */

	if (iwin->rows > 50)
		ytics = datarows / 8;
	else
		ytics = datarows / 4;

	for (y = 0; y < datarows; y += ytics) {
		v = dir ?
			max - (max - min) * (double)y / (datarows) :
			min + (max - min) * (double)y / (datarows);
		if ( ! dumb)
			printf("\033[%zu;%zuH%lc", 
				iwin->top + y + 1, 
				iwin->left - 1 + 1, L'\x251c');
		else
			printf("\033[%zu;%zuH-", 
				iwin->top + y + 1, 
				iwin->left - 1 + 1);
        	printf("\033[%zu;%zuH%*.1f", 
			iwin->top + y + 1, 
			iwin->left - lbuf + 1, (int)lbuf - 1, v);
	}

	/* Make sure we have a value at the maximum. */

	if (y >= datarows) {
		v = dir ? max - (max - min) : min + (max - min);
        	printf("\033[%zu;%zuH%*.1f", 
			iwin->top + (iwin->rows - 2) + 1, 
			iwin->left - lbuf + 1, (int)lbuf - 1,
			v);
		if ( ! dumb)
			printf("\033[%zu;%zuH%lc", 
				iwin->top + (iwin->rows - 2) + 1, 
				iwin->left - 1 + 1, L'\x251c');
		else
			printf("\033[%zu;%zuH-", 
				iwin->top + (iwin->rows - 2) + 1, 
				iwin->left - 1 + 1);
	}

	if ( ! dumb)
		printf("\033[%zu;%zuH%lc", 
			iwin->top + iwin->rows, 
			iwin->left - 1 + 1, L'\x2514');
	else
		printf("\033[%zu;%zuH\\", 
			iwin->top + iwin->rows, 
			iwin->left - 1 + 1);
	/* 
	 * Now make the x-axis and x-axis label.
	 * Again, start with the border.
	 * Then the tics.
	 */

	for (x = 0; x < iwin->cols; x++)
		if ( ! dumb)
			printf("\033[%zu;%zuH%lc", 
				iwin->top + iwin->rows, 
				iwin->left + x + 1, L'\x2500');
		else
			printf("\033[%zu;%zuH-", 
				iwin->top + iwin->rows, 
				iwin->left + x + 1);

	if (iwin->cols > 100)
		xtics = (iwin->cols - 6) / 8;
	else
		xtics = (iwin->cols - 6) / 4;

	for (x = 0; x < iwin->cols; x += xtics) {
		t = (maxt - mint) * 
			((double)x / iwin->cols);
		if ( ! aggr)
			t += mint;
		if ( ! dumb)
			printf("\033[%zu;%zuH%lc", 
				iwin->top + iwin->rows, 
				iwin->left + x + 1, L'\x253c');
		else
			printf("\033[%zu;%zuH|", 
				iwin->top + iwin->rows, 
				iwin->left + x + 1);
        	printf("\033[%zu;%zuH%03zu:%02zu", 
			iwin->top + iwin->rows + 1, 
			iwin->left + x + 1,
			t / 60, t % 60);
	}

	/* Draw graph values, if applicable. */

	for (i = 0; i < avgsz; i++)
		for (x = 0; x < iwin->cols; x++) {
			if (0 == avgs[i][x].sz)
				continue;
			v = avgs[i][x].accum / (double)avgs[i][x].sz;
			y = dir ?
				(iwin->rows - 2) * 
				((max - v) / (max - min)) :
				(iwin->rows - 2) * 
				((v - min) / (max - min));

			if ( ! dumb)
				printf("\033[1;3%zum\033[%zu;%zuH%lc", 
					(i % 7) + 1,
					iwin->top + y + 1, 
					iwin->left + x + 1, L'\x2022');
			else
				printf("\033[1;3%zum\033[%zu;%zuH+", 
					(i % 7) + 1,
					iwin->top + y + 1, 
					iwin->left + x + 1);
		}

	/* Reset terminal attributes. */

        printf("\033[0m");
}

/*
 * Sort values into the column-width bins.
 * Returns the bins allocated of size "cols".
 */
static struct avg *
collect(const struct dive *d, size_t cols, 
	time_t mint, time_t maxt, enum grapht type)
{
	struct samp	*samp;
	double		 frac;
	size_t		 idx;
	time_t		 t;
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
		
		/*
		 * If we're aggregating, the bucket is going to be
		 * as a fraction of the total time (mint, maxt).
		 * Otherwise it's just between 0 and maxt.
		 */

		t = aggr ? (d->datetime + samp->time) - mint : 
			samp->time;
		frac = (double)t / (maxt - mint);
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
print_all(const struct diveq *dq, const struct winsize *ws)
{
	const struct dive *d;
	struct graph	   temp, depth;
	struct samp	  *samp;
	int		   dtemp = 0, ddepth = 0;
	struct win	   win, iwin;
	size_t		   i, lbuf, avgsz, tbuf, need;
	struct avg	 **avg;
	time_t		   mint = 0, maxt = 0, t;

	memset(&temp, 0, sizeof(struct graph));
	memset(&depth, 0, sizeof(struct graph));

	depth.minvalue = temp.minvalue = DBL_MAX;
	depth.maxvalue = temp.maxvalue = -DBL_MAX;

	assert( ! TAILQ_EMPTY(dq));

	/*
	 * Determine the boundaries for the depth and temperature.
	 * While here, also compute the number of samples for both of
	 * these measurements.
	 */

	avgsz = 0;
	TAILQ_FOREACH(d, dq, entries) {
		if (aggr && 0 == d->datetime) {
			warnx("%s:%zu: datetime required",
				d->log->file, d->line);
			continue;
		}
		avgsz++;
		TAILQ_FOREACH(samp, &d->samps, entries) {
			if (SAMP_DEPTH & samp->flags) {
				depth.nsamps++;
				if (samp->depth > depth.maxvalue)
					depth.maxvalue = samp->depth;
				if (samp->depth < depth.minvalue)
					depth.minvalue = samp->depth;
			}
			if (SAMP_TEMP & samp->flags) {
				temp.nsamps++;
				if (samp->temp > temp.maxvalue)
					temp.maxvalue = samp->temp;
				if (samp->temp < temp.minvalue)
					temp.minvalue = samp->temp;
			}
		}
	}

	/*
	 * Next, accumulate time boundaries.
	 * For non-aggregate measurement, we're going to just look for
	 * maximum duration.
	 * For aggregate measurement, we compute the maximum by
	 * offsetting from the minimum value.
	 * We only allow dives with dates and times for aggregates.
	 */

	if (aggr) {
		TAILQ_FOREACH(d, dq, entries)
			if (d->datetime &&
			    (0 == mint || d->datetime < mint))
				mint = d->datetime;
		TAILQ_FOREACH(d, dq, entries) {
			if (0 == d->datetime)
				continue;
			TAILQ_FOREACH(samp, &d->samps, entries)
				if (SAMP_TEMP & samp->flags ||
				    SAMP_DEPTH & samp->flags) {
					t = d->datetime + samp->time;
					if (t > maxt)
						maxt = t;
				}
		}
	} else
		TAILQ_FOREACH(d, dq, entries) 
			TAILQ_FOREACH(samp, &d->samps, entries)
				if (SAMP_DEPTH & samp->flags ||
				    SAMP_TEMP & samp->flags) {
					t = samp->time;
					if (t > maxt)
						maxt = t;
				}

	/*
	 * Establish whether we should do any graphing at all for the
	 * temperature or depth.
	 * Requirements are (1) at least 2 samples and (2) a non-zero
	 * difference between maximum and minimum.
	 */

	if (temp.nsamps >= 2 && 
	    fabs(temp.maxvalue - temp.minvalue) > FLT_EPSILON)
		dtemp = 1;
	if (depth.nsamps >= 2 && 
	    fabs(depth.maxvalue - depth.minvalue) > FLT_EPSILON)
		ddepth = 1;

	if (0 == showtemp && dtemp)
		dtemp = 0;

	if (0 == dtemp && 0 == ddepth) {
		warnx("no data points to graph");
		return(0);
	}

	if (NULL == (avg = calloc(avgsz, sizeof(struct avg *))))
		err(EXIT_FAILURE, NULL);

	/*
	 * Make some decisions on what we should show.
	 * If we want a legend and we don't have enough space to do any
	 * graphing, then disable the legend.
	 */


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
	 * Make some allowances.
	 * If we're requesting both graphs and don't have enough space
	 * for both graphs (7 for each, including top buffer), then
	 * disable the legend.
	 * If we're requesting one graph and don't have space, disable
	 * the legend.
	 */

	need = (showlegend ? avgsz + 1 : 0) +
		(dtemp ? 6 : 0) +
		(ddepth ? 6 : 0);

	if (dtemp && ddepth)
		need += 2;

	if (need >= win.rows) {
		warnx("not enough output rows");
		free(avg);
		return(0);
	}

	if (dtemp && ddepth && 0 == win.rows % 2)
		win.rows--;

	printf("\e[1;1H\e[2J");

	/* Now do the actual printing. */

	if (showlegend) 
		print_legend(dq, &win);

	tbuf = showlegend ? avgsz + 1 : 0;
	lbuf = (dtemp && temp.maxvalue >= 100.0) || 
	       (ddepth && depth.maxvalue >= 100.0) ? 6 : 5;

	if (dtemp) {
		iwin = win;
		iwin.cols -= lbuf;
		iwin.left += lbuf;
		iwin.top += tbuf;
		if (ddepth)
			iwin.rows = (win.rows / 2) - (tbuf / 2) - 1;
		else
			iwin.rows -= tbuf;

		i = 0;
		TAILQ_FOREACH(d, dq, entries) 
			avg[i++] = collect(d, iwin.cols, 
				mint, maxt, GRAPH_TEMP);

		print_avgs((const struct avg *const *)avg, avgsz, 
			&iwin, temp.minvalue, temp.maxvalue, 
			mint, maxt, lbuf, 1);
		for (i = 0; i < avgsz; i++)
			free(avg[i]);
	}

	if (ddepth) {
		iwin = win;
		iwin.cols -= lbuf;
		iwin.left += lbuf;
		if (dtemp) {
			iwin.top = (win.rows / 2) + (tbuf / 2) + 3;
			iwin.rows = (win.rows / 2) - (tbuf / 2) - 1;
		} else {
			iwin.top += tbuf;
			iwin.rows -= tbuf;
		}

		i = 0;
		TAILQ_FOREACH(d, dq, entries)
			avg[i++] = collect(d, iwin.cols, 
				mint, maxt, GRAPH_DEPTH);
		print_avgs((const struct avg *const *)avg, 
			avgsz, &iwin, 0, depth.maxvalue, 
			mint, maxt, lbuf, 0);
		for (i = 0; i < avgsz; i++)
			free(avg[i]);
	}

	/* Put as at the bottom of the screen. */

        printf("\033[%d;%dH", ws->ws_row, 0);

	free(avg);
	return(1);
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 0;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct divestat	 st;
	struct winsize	 ws;

	setlocale(LC_ALL, "");
	memset(&ws, 0, sizeof(struct winsize));

	/* Do our best to get terminal dimensions. */

	if (-1 == ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)) {
		if (NULL != getenv("LINES"))
			ws.ws_row = atoi(getenv("LINES"));
		if (NULL != getenv("COLUMNS"))
			ws.ws_col = atoi(getenv("COLUMNS"));
	}

#if HAVE_PLEDGE
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (c = getopt(argc, argv, "alntv")))
		switch (c) {
		case ('a'):
			aggr = 1;
			break;
		case ('l'):
			showlegend = 1;
			break;
		case ('n'):
			dumb = 1;
			break;
		case ('t'):
			showtemp = 1;
			break;
		case ('v'):
			verbose = 1;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	divecmd_init(&p, &dq, &st, 
		GROUP_NONE, GROUPSORT_DATETIME);

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

	/* Establish minima. */

	if ( ! TAILQ_EMPTY(&dq))
		rc = print_all(&dq, &ws);

	/* Free all memory from the dives. */
out:
	divecmd_free(&dq, &st);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-alntv] [file]\n", getprogname());
	return(EXIT_FAILURE);
}
