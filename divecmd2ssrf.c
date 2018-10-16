/*	$Id$ */
/*
 * Copyright (c) 2018 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <expat.h>

#include "parser.h"

int verbose = 0;

static void
print_all(const struct dlog *dl, const struct diveq *dq)
{
	const struct dive *d;
	const struct samp *s;
	struct tm	  *tm;

	printf("<divelog program=\'dcmd2ssrf\' "
	       " version=\'" VERSION "\'>\n"
	       " <settings>\n"
	       "  <divecomputerid model=\'%s %s\'/>\n"
	       " </settings>\n"
	       " <divesites>\n"
	       " </divesites>\n"
	       " <dives>\n",
	       dl->vendor, dl->product);

	TAILQ_FOREACH(d, dq, entries) {
		printf("  <dive number='%zu'", d->num);
		if (d->datetime) {
			tm = localtime(&d->datetime);
			printf(" date='%.4d-%.2d-%.2d'"
			       " time='%.2d:%.2d:%.2d'",
			       tm->tm_year + 1900, 
			       tm->tm_mon + 1, tm->tm_mday,
			       tm->tm_hour, tm->tm_min,
			       tm->tm_sec);
		}
		if (d->duration)
			printf(" duration='%zu:%.2zu min'",
				d->duration / 60,
				d->duration % 60);
		puts(">");
		printf("   <divecomputer model='%s %s'>\n",
			d->log->vendor, d->log->product);
		TAILQ_FOREACH(s, &d->samps, entries) {
			printf("    <sample time=\'%zu:%.2zu min\' ",
				s->time / 60,
				s->time % 60);
			if (SAMP_DEPTH & s->flags) 
				printf(" depth=\'%.1f m\'", s->depth);
			if (SAMP_TEMP & s->flags) 
				printf(" temp=\'%.1f C\'", s->temp);
			if (SAMP_RBT & s->flags) 
				printf(" rbt=\'%zu:%.2zu min\'", 
					s->rbt / 60, s->rbt % 60);
			if (SAMP_CNS & s->flags) 
				printf(" cns=\'%.0f %%\'", 
					100.0 * s->cns);
			puts(" />");
		}
		puts("   </divecomputer>\n"
		     "  </dive>\n");
	}

	puts(" </dives>\n"
	     "</divelog>");
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 1;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct divestat	 st;

#if HAVE_PLEDGE
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

	divecmd_init(&p, &dq, &st, GROUP_DIVER, GROUPSORT_DATETIME);

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
	
	if (NULL != TAILQ_NEXT(TAILQ_FIRST(&st.dlogs), entries)) {
		warnx("only one computer/diver allowed");
		goto out;
	}

	print_all(TAILQ_FIRST(&st.dlogs), &dq);
out:
	divecmd_free(&dq, &st);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-v] [file]\n", getprogname());
	return(EXIT_FAILURE);
}
