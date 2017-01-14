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
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <expat.h>

#include "parser.h"

int verbose = 0;

static int aggr = 0;

static void
print_all(const struct diveq *dq)
{
	struct dive	*d;
	struct samp	*s;

	assert( ! TAILQ_EMPTY(dq));

	puts(".G1");
	puts("draw solid");

	TAILQ_FOREACH(d, dq, entries) {
		TAILQ_FOREACH(s, &d->samps, entries)
			if (SAMP_DEPTH & s->flags)
				printf("%zu -%g\n", s->time, s->depth);
		if (TAILQ_NEXT(d, entries)) 
			puts("new");
	}

	puts(".G2");
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 1;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;

	/* Pledge us early: only reading files. */

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (c = getopt(argc, argv, "av")))
		switch (c) {
		case ('a'):
			aggr = 1;
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
		rc = parse("-", p, &dq);

	for (i = 0; i < (size_t)argc; i++)
		if ( ! (rc = parse(argv[i], p, &dq)))
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

	print_all(&dq);
out:
	parse_free(&dq);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-av] [file]\n", getprogname());
	return(EXIT_FAILURE);
}
