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
#include <time.h>
#include <unistd.h>

#include <expat.h>

#include "parser.h"

int verbose = 0;

static void
print_divelog(const struct dlog *dl)
{
	int	 ns = 0;

	printf("divelog: ");
	if (NULL != dl->vendor) {
		printf("%s%s", ns ? ", " : "", dl->vendor);
		ns = 1;
	}
	if (NULL != dl->product) {
		printf("%s%s", ns ? ", " : "", dl->product);
		ns = 1;
	}
	if (NULL != dl->model) {
		printf("%s%s", ns ? ", " : "", dl->model);
		ns = 1;
	}
	if (NULL != dl->ident)
		printf("%s%s", ns ? ": " : "", dl->ident);
	putchar('\n');
}

static void
print_datetime(const struct dive *d)
{
	struct tm	*tm = localtime(&d->datetime);

	printf("  %04d-%02d-%02d %02d:%02d:%02d  ",
		tm->tm_year + 1900, tm->tm_mon + 1, 
		tm->tm_mday, tm->tm_hour, tm->tm_min, 
		tm->tm_sec);
}

static void
print_duration(int human, const struct dive *d)
{

	if (human) {
		if (d->maxtime >= 60 * 60) 
			printf("%3zu:%.2zu:%.2zu",
				d->maxtime / 60 / 60,
				(d->maxtime % (60 * 60)) / 60,
				d->maxtime % 60);
		else if (d->maxtime >= 60) 
			printf("%6zu:%.2zu", 
				d->maxtime / 60, 
				d->maxtime % 60);
		else
			printf("%9zu", d->maxtime);
	} else
		printf("%6zu", d->maxtime);

	putchar(' ');
	putchar(' ');
}

static void
print_all(int human, const struct divestat *ds)
{
	const struct dgroup	*dg;
	const struct dive	*d;
	const struct samp	*s;
	size_t			 i, tempsz;
	double			 temp;

	for (i = 0; i < ds->groupsz; i++) {
		dg = ds->groups[i];
		print_divelog(TAILQ_FIRST(&dg->dives)->log);
		TAILQ_FOREACH(d, &dg->dives, gentries) {
			tempsz = 0;
			temp = 0.0;
			TAILQ_FOREACH(s, &d->samps, entries)
				if (SAMP_TEMP & s->flags) {
					temp += s->temp;
					tempsz++;
				}
			print_datetime(d);
			printf("%5.2f  ", d->maxdepth);
			if (tempsz > 0)
				printf("%5.1f  ", temp / tempsz);
			else
				fputs("    -  ", stdout);
			print_duration(human, d);
			if (MODE_FREEDIVE == d->mode)
				puts("free");
			else if (MODE_GAUGE == d->mode)
				puts("gauge");
			else if (MODE_OC == d->mode)
				puts("open");
			else if (MODE_CC == d->mode)
				puts("closed");
		}
	}
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 1, human = 0;
	const char	*sort = NULL;
	enum groupsort	 gsort;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct divestat	 st;

#if HAVE_PLEDGE
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (c = getopt(argc, argv, "hs:v")))
		switch (c) {
		case ('h'):
			human = 1;
			break;
		case ('s'):
			sort = optarg;
			break;
		case ('v'):
			verbose = 1;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	if (NULL == sort ||
	    0 == strcasecmp(sort, "datetime"))
		gsort = GROUPSORT_DATETIME;
	else if (0 == strcasecmp(sort, "maxtime"))
		gsort = GROUPSORT_MAXTIME;
	else if (0 == strcasecmp(sort, "rmaxtime"))
		gsort = GROUPSORT_RMAXTIME;
	else if (0 == strcasecmp(sort, "maxdepth"))
		gsort = GROUPSORT_MAXDEPTH;
	else if (0 == strcasecmp(sort, "rmaxdepth"))
		gsort = GROUPSORT_RMAXDEPTH;
	else
		goto usage;

	divecmd_init(&p, &dq, &st, GROUP_DIVELOG, gsort);

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

	print_all(human, &st);
out:
	divecmd_free(&dq, &st);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);

usage:
	fprintf(stderr, "usage: %s [-hv] [-s sort] [file ...]\n", 
		getprogname());
	return(EXIT_FAILURE);
}
