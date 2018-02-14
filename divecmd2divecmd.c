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
#if HAVE_ERR
# include <err.h>
#endif
#include <limits.h>
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

int verbose = 0;

static FILE *
file_open(const struct dive *d, const char *out)
{
	char	 path[PATH_MAX];
	FILE	*f;

	snprintf(path, sizeof(path), 
		"%s/dive-%lld.xml", out, d->datetime);

	if (NULL != (f = fopen(path, "w")))
		return(f);

	warn("%s", path);
	return(NULL);
}

/*
 * See print_dive_open().
 */
static void
print_dive_close(FILE *f)
{

	divecmd_print_dive_sampleq_close(f);
	divecmd_print_dive_close(f);
}

/*
 * Print the heading for a particular dive.
 * Set the start date at "t", being dive "num", in mode "mode".
 * Don't print any other attributes of the dive.
 * (These are the critical ones.)
 * Follow with print_dive_close().
 */
static void
print_dive_open(FILE *f, time_t t, size_t num, enum mode mode)
{
	struct dive	 dive;

	memset(&dive, 0, sizeof(struct dive));
	dive.datetime = t;
	dive.num = num;
	dive.mode = mode;

	divecmd_print_dive_open(f, &dive);
	divecmd_print_dive_sampleq_open(f);
}

static void
print_dive(FILE *f, const struct dive *d)
{

	divecmd_print_dive_open(f, d);
	divecmd_print_dive_fingerprint(f, d);
	divecmd_print_dive_sampleq(f, &d->samps);
	divecmd_print_dive_close(f);
}

/*
 * Put all dive samples into the same dive profile.
 * We insert "top-side" dives during the surface interval between dive
 * sets.
 */
static void
print_join(FILE *f, const struct dive *d, time_t *last, time_t first)
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
			tmp.time = *last - first;
			divecmd_print_dive_sample(f, &tmp);
			*last += span;
		} while (*last < d->datetime);
	}

	/* Now print the samples. */

	TAILQ_FOREACH(s, &d->samps, entries) {
		tmp = *s;
		tmp.time = (s->time + d->datetime) - first;
		divecmd_print_dive_sample(f, &tmp);
		*last = s->time + d->datetime;
	}
}

/*
 * Given a single dive entry, split it into multiple dive entries when
 * we have two consecutive dives <1 metre.
 * We then ignore samples until the next dive is >1 metre.
 */
static void
print_split(const struct dive *d, 
	const char *out, size_t *num, enum mode mode)
{
	time_t		 start;
	double		 lastdepth = 100.0;
	const struct samp *s;
	struct samp	 tmp;
	int	 	 dopen = 0;
	FILE		*f = stdout;

	start = d->datetime;
	s = TAILQ_FIRST(&d->samps);
	assert(NULL != s);

again:
	for ( ; NULL != s; s = TAILQ_NEXT(s, entries)) {
		if ( ! (SAMP_DEPTH & s->flags) &&
		     ! (SAMP_TEMP & s->flags))
			continue;

		if (0 == dopen) {
			if (NULL != out) {
				if (NULL == (f = file_open(d, out)))
					return;
				divecmd_print_open(f, d->log);
				divecmd_print_diveq_open(f);
			}
			print_dive_open(f, start, (*num)++, mode);
			dopen = 1;
		}

		/* Print sample data. */

		tmp = *s;
		tmp.time = (s->time + d->datetime) - start;
		divecmd_print_dive_sample(f, &tmp);

		/* Are we still "at depth"? */

		if ( ! (SAMP_DEPTH & s->flags))
			continue;

		if (lastdepth >= 1.0 || s->depth >= 1.0) {
			lastdepth = s->depth;
			continue;
		}

		print_dive_close(f);

		if (NULL != out) {
			divecmd_print_diveq_close(f);
			divecmd_print_close(f);
			fclose(f);
			f = stdout;
		}

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
		print_dive_close(f);
		if (NULL != out) {
			divecmd_print_diveq_close(f);
			divecmd_print_close(f);
			fclose(f);
			f = stdout;
		}
	}
}

/*
 * Take a single input file and either split or join it.
 */
static int
print_all(enum pmode pmode, const char *out, const struct diveq *dq)
{
	const struct dive *d;
	size_t		 num = 1;
	time_t		 last, first;
	enum mode	 mode;
	FILE		*f = stdout;
	
	TAILQ_FOREACH(d, dq, entries)
		if (0 == d->datetime) {
			warnx("no dive timestamp"); 
			return(0);
		}

	if (PMODE_NONE == pmode) {
		if (NULL == out) {
			divecmd_print_open(f, TAILQ_FIRST(dq)->log);
			divecmd_print_diveq_open(f);
		}
		TAILQ_FOREACH(d, dq, entries) {
			if (NULL != out) {
				if (NULL == (f = file_open(d, out)))
					return(0);
				divecmd_print_open(f, d->log);
				divecmd_print_diveq_open(f);
			}
			print_dive(f, d);
			if (NULL != out) {
				divecmd_print_diveq_close(f);
				divecmd_print_close(f);
				fclose(f);
				f = stdout;
			}
		}
		if (NULL == out) {
			divecmd_print_diveq_close(f);
			divecmd_print_close(f);
		}
		return(1);
	}

	if (PMODE_SPLIT == pmode) {
		if (NULL == out) {
			divecmd_print_open(f, TAILQ_FIRST(dq)->log);
			divecmd_print_diveq_open(f);
		}
		TAILQ_FOREACH(d, dq, entries)
			print_split(d, out, &num, d->mode);
		if (NULL == out) {
			divecmd_print_diveq_close(f);
			divecmd_print_close(f);
		}
	} else {
		d = TAILQ_FIRST(dq);
		if (NULL != out) 
			if (NULL == (f = file_open(d, out)))
				return(0);
		divecmd_print_open(f, d->log);
		divecmd_print_diveq_open(f);
		mode = d->mode;
		print_dive_open(f, d->datetime, 1, mode);
		first = d->datetime;
		last = 0;
		TAILQ_FOREACH(d, dq, entries) {
			if (d->mode != mode)
				warnx("mode mismatch");
			print_join(f, d, &last, first);
		}
		print_dive_close(f);
		divecmd_print_diveq_close(f);
		divecmd_print_close(f);
		if (NULL != out) {
			fclose(f);
			f = stdout;
		}
	}

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
	struct divestat	 st;
	const char	*out = NULL;

#if HAVE_PLEDGE
	if (-1 == pledge("stdio rpath wpath cpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (c = getopt(argc, argv, "o:jsv")))
		switch (c) {
		case ('j'):
			mode = PMODE_JOIN;
			break;
		case ('o'):
			out = optarg;
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

	divecmd_init(&p, &dq, &st, GROUP_NONE);

	if (0 == argc)
		rc = divecmd_parse("-", p, &dq, &st);

	for (i = 0; i < (size_t)argc; i++)
		if ( ! (rc = divecmd_parse(argv[i], p, &dq, &st)))
			break;

#if HAVE_PLEDGE
	if (NULL == out) {
		if (-1 == pledge("stdio", NULL))
			err(EXIT_FAILURE, "pledge");
	} else {
		if (-1 == pledge("stdio wpath cpath", NULL)) 
			err(EXIT_FAILURE, "pledge");
	}
#endif

	XML_ParserFree(p);

	if ( ! rc)
		goto out;

	if (TAILQ_EMPTY(&dq)) {
		warnx("no dives to display");
		goto out;
	}

	rc = print_all(mode, out, &dq);
out:
	divecmd_free(&dq, &st);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s "
		"[-jsv] "
		"[-o out] "
		"[file ...]\n", getprogname());
	return(EXIT_FAILURE);
}
