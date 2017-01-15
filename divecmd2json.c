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

static int aggr = 0;

static void
print_all(const struct diveq *dq, const struct divestat *st)
{
	struct dive	*d;
	struct samp	*s;
	time_t		 t;

	assert( ! TAILQ_EMPTY(dq));

	puts("{\"divecmd2json\":");
	puts("\t{\"version\": \"" VERSION "\",");
	puts("\t \"divers\": [");
	puts("\t\t{\"dives\": [");

	TAILQ_FOREACH(d, dq, entries) {
		printf("\t\t\t{\"num\": %zu,\n", d->num);
		if (0 != d->duration)
			printf("\t\t\t \"duration\": %zu,\n", 
				d->duration);
		if (0 != d->datetime)
			printf("\t\t\t \"datetime\": %lld,\n", 
				d->datetime);
		puts("\t\t\t \"samples\": [");
		TAILQ_FOREACH(s, &d->samps, entries) {
			t = s->time;
			if (aggr) {
				t += d->datetime;
				t -= st->timestamp_min;
			}
			printf("\t\t\t\t{\"time\": %lld", t);
			if (SAMP_DEPTH & s->flags)
				printf(", \"depth\": %g", s->depth);
			if (SAMP_TEMP & s->flags)
				printf(", \"temp\": %g", s->temp);
			printf("}%s\n", 
				TAILQ_NEXT(s, entries) ? "," : "");
		}
		puts("\t\t\t\t]");
		if (TAILQ_NEXT(d, entries)) 
			puts("\t\t\t},");
		else
			puts("\t\t\t}]");
	}
	puts("\t\t}]");
	puts("\t}");
	puts("}");
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 1;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct divestat	 st;

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif
	
	memset(&st, 0, sizeof(struct divestat));

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

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	print_all(&dq, &st);
out:
	parse_free(&dq);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-av] [file]\n", getprogname());
	return(EXIT_FAILURE);
}
