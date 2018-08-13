/*	$Id$ */
/*
 * Copyright (c) 2017--2018 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <expat.h>

#include "parser.h"

enum	limit {
	LIMIT_DATE_AFTER,
	LIMIT_DATE_BEFORE,
	LIMIT_DATE_EQ,
	LIMIT_DATETIME_AFTER,
	LIMIT_DATETIME_BEFORE,
	LIMIT_DIVE_EQ,
	LIMIT_MODE_EQ
};

TAILQ_HEAD(limitq, limits);

/*
 * Limits constrain which dives we show.
 * There are many possible limits to choose from.
 */
struct	limits {
	enum limit	 type; /* type of constraint */
	time_t		 date; /* date/time, if applicable */
	enum mode	 mode; /* mode, if applicable */
	size_t		 pid; /* parse id, if applicable */
	TAILQ_ENTRY(limits) entries;
};

int verbose = 0;

static int
limit_match(const struct dive *d, const struct limitq *lq)
{
	const struct limits *l;

	TAILQ_FOREACH(l, lq, entries) {
		switch (l->type) {
		case LIMIT_DATE_EQ:
			if (0 == d->datetime)
				return 0;
			if (d->datetime < l->date ||
			    d->datetime > l->date + 60 * 60 * 24)
				return 0;
			continue;
		case LIMIT_DATE_BEFORE:
		case LIMIT_DATETIME_BEFORE:
			if (0 == d->datetime)
				return 0;
			if (d->datetime > l->date)
				return 0;
			continue;
		case LIMIT_DATE_AFTER:
		case LIMIT_DATETIME_AFTER:
			if (0 == d->datetime)
				return 0;
			if (d->datetime < l->date)
				return 0;
			continue;
		case LIMIT_DIVE_EQ:
			if (d->pid != l->pid)
				return 0;
			break;
		case LIMIT_MODE_EQ:
			if (l->mode != d->mode)
				return 0;
			continue;
		}
	}

	return 1;
}

static FILE *
file_open(const struct dive *d, const char *out)
{
	char	 path[PATH_MAX];
	FILE	*f;

	snprintf(path, sizeof(path), 
		"%s/dive-%lld.xml", out, d->datetime);

	if (NULL == (f = fopen(path, "w")))
		err(EXIT_FAILURE, "%s", path);

	return f;
}

static uint32_t 
jhash(const char *key)
{
	size_t 	 i = 0, length = strlen(key);
	uint32_t hash = 0;

	while (i != length) {
		hash += key[i++];
		hash += hash << 10;
		hash ^= hash >> 6;
	}
	hash += hash << 3;
	hash ^= hash >> 11;
	hash += hash << 15;
	return hash;
}

static int
stringeq(const char *p1, const char *p2)
{

	if ((NULL == p1 && NULL != p2) ||
	    (NULL != p1 && NULL == p2))
		return 0;
	if (NULL == p1 && NULL == p2)
		return 1;
	return 0 == strcmp(p1, p2);
}

static int
dlogeq(const struct dlog *d1, const struct dlog *d2)
{

	return stringeq(d1->vendor, d2->vendor) &&
	       stringeq(d1->product, d2->product) &&
	       stringeq(d1->model, d2->model);
}

/*
 * Take a single input file and either split or join it.
 */
static int
print_all(const char *out, const struct limitq *lq, 
	const struct diveq *dq)
{
	const struct dive   *d;
	const struct dlog   *dl;
	size_t		     idx, i;
	FILE		    *f = stdout;
	const struct dive ***htab = NULL;
	const size_t 	     htabsz = 4096;
	
	TAILQ_FOREACH(d, dq, entries)
		if (0 == d->datetime) {
			warnx("%s:%zu: no <dive> timestamp",
				d->log->file, d->line);
			return 0;
		}

	/*
	 * If we don't have a mode specified, simply print out each dive
	 * into its own file (if "out" is specified) or print them back
	 * to stdout as one big standalone file.
	 * They must be from the same divelog (i.e., dive computer),
	 * however, and the fingerprints must also be unique.
	 */

	htab = calloc(htabsz, sizeof(struct dive **));
	if (NULL == htab)
		err(EXIT_FAILURE, NULL);
	if (NULL == out) {
		divecmd_print_open(f, TAILQ_FIRST(dq)->log);
		divecmd_print_diveq_open(f);
	}
	assert(NULL != TAILQ_FIRST(dq));
	dl = TAILQ_FIRST(dq)->log;
	TAILQ_FOREACH(d, dq, entries) {
		if ( ! dlogeq(dl, d->log)) {
			warnx("%s:%zu: dive has mismatched "
				"computer (from %s:%zu)",
				d->log->file, d->line, 
				dl->file, dl->line);
			continue;
		} else if ( ! limit_match(d, lq))
			continue;

		/* Look up in hashtable. */

		if (NULL == d->fprint) {
			warnx("%s:%zu: no <fingerprint>",
				d->log->file, d->line);
			continue;
		}

		idx = jhash(d->fprint) % htabsz;
		if (NULL == htab[idx]) {
			htab[idx] = calloc
				(1, sizeof(struct dive *));
			if (NULL == htab[idx])
				err(EXIT_FAILURE, NULL);
		} 

		for (i = 0; NULL != htab[idx][i]; i++)
			if (0 == strcmp
			    (htab[idx][i]->fprint, d->fprint))
				break;

		if (NULL != htab[idx][i]) {
			warnx("%s:%zu: duplicate dive from "
				"%s:%zu", d->log->file, 
				d->line, 
				htab[idx][i]->log->file,
				htab[idx][i]->line);
			continue;
		} 

		/* Add to hash line. */

		htab[idx] = reallocarray
			(htab[idx], i + 2, sizeof(struct dive *));
		if (NULL == htab[idx])
			err(EXIT_FAILURE, NULL);
		htab[idx][i] = d;
		htab[idx][i + 1] = NULL;

		/* Print dive. */

		if (NULL != out) {
			f = file_open(d, out);
			divecmd_print_open(f, d->log);
			divecmd_print_diveq_open(f);
		}
		divecmd_print_dive(f, d);
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
	for (i = 0; i < htabsz; i++)
		free(htab[i]);
	free(htab);

	return 1;
}

static int
limit_parse(const char *arg, struct limits *l)
{
	const char 	*obj, *cp;
	struct tm	 tm;
	struct tm	*tmp;
	time_t		 t;
	const char	*er = NULL;

	memset(l, 0, sizeof(struct limits));

	if (0 == strncmp("dafter=", optarg, 7)) {
		l->type = LIMIT_DATE_AFTER;
		obj = optarg + 7;
	} else if (0 == strncmp("dbefore=", optarg, 8)) {
		l->type = LIMIT_DATE_BEFORE;
		obj = optarg + 8;
	} else if (0 == strncmp("dtafter=", optarg, 8)) {
		l->type = LIMIT_DATETIME_AFTER;
		obj = optarg + 8;
	} else if (0 == strncmp("dtbefore=", optarg, 9)) {
		l->type = LIMIT_DATETIME_BEFORE;
		obj = optarg + 9;
	} else if (0 == strncmp("date=", optarg, 5)) {
		l->type = LIMIT_DATE_EQ;
		obj = optarg + 5;
	} else if (0 == strncmp("dive=", optarg, 5)) {
		l->type = LIMIT_DIVE_EQ;
		obj = optarg + 5;
	} else if (0 == strncmp("mode=", optarg, 5)) {
		l->type = LIMIT_MODE_EQ;
		obj = optarg + 5;
	} else {
		warnx("-l: unknown predicate: %s", optarg);
		return 0;
	}

	while (isspace((unsigned char)*obj))
		obj++;

	if ('\0' == *obj) {
		warnx("-l: empty predicate: %s", optarg);
		return 0;
	}

	switch (l->type) {
	case LIMIT_DATETIME_AFTER:
	case LIMIT_DATETIME_BEFORE:
		memset(&tm, 0, sizeof(struct tm));
		cp = strptime(obj, "%Y-%m-%dT%R", &tm);
		if (NULL != cp && '\0' == *cp) {
			l->date = mktime(&tm);
			break;
		}
		warnx("-l: bad datetime: %s", obj);
		return 0;
	case LIMIT_DATE_EQ:
	case LIMIT_DATE_AFTER:
	case LIMIT_DATE_BEFORE:
		if (0 == strcasecmp(obj, "today")) {
			t = time(NULL);
			tmp = localtime(&t);
			tmp->tm_sec = tmp->tm_min =
				tmp->tm_hour = 0;
			l->date = mktime(tmp);
			break;
		} else if (0 == strcasecmp(obj, "yesterday")) {
			t = time(NULL) - 60 * 60 * 24;
			tmp = localtime(&t);
			tmp->tm_sec = tmp->tm_min =
				tmp->tm_hour = 0;
			l->date = mktime(tmp);
			break;
		}
		memset(&tm, 0, sizeof(struct tm));
		cp = strptime(obj, "%Y-%m-%d", &tm);
		if (NULL != cp && '\0' == *cp) {
			l->date = mktime(&tm);
			break;
		}
		warnx("-l: bad date: %s", obj);
		return 0;
	case LIMIT_DIVE_EQ:
		l->pid = strtonum(obj, 0, LONG_MAX, &er);
		if (NULL == er) {
			warnx("pid: %zu", l->pid);
			break;
		}
		warnx("-l: bad pid: %s: %s", obj, er);
		return 0;
	case LIMIT_MODE_EQ:
		if (0 == strcasecmp(obj, "open")) {
			l->mode = MODE_OC;
			break;
		} else if (0 == strcasecmp(obj, "closed")) {
			l->mode = MODE_CC;
			break;
		} else if (0 == strcasecmp(obj, "gauge")) {
			l->mode = MODE_GAUGE;
			break;
		} else if (0 == strcasecmp(obj, "free")) {
			l->mode = MODE_FREEDIVE;
			break;
		} 
		warnx("-l: bad mode: %s", obj);
		return 0;
	}

	return 1;
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 1;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct divestat	 st;
	const char	*out = NULL;
	struct limitq	 limits;
	struct limits	*l, tmp;

	TAILQ_INIT(&limits);

#if HAVE_PLEDGE
	if (-1 == pledge("stdio rpath wpath cpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (c = getopt(argc, argv, "jl:o:sv")))
		switch (c) {
		case 'l':
			if ( ! limit_parse(optarg, &tmp))
				goto usage;
			l = calloc(1, sizeof(struct limits));
			if (NULL == l)
				err(EXIT_FAILURE, NULL);
			*l = tmp;
			TAILQ_INSERT_TAIL(&limits, l, entries);
			break;
		case 'o': /* XXX: undocumented */
			out = optarg;
			break;
		case 'v':
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

	rc = print_all(out, &limits, &dq);

out:
	while (NULL != (l = TAILQ_FIRST(&limits))) {
		TAILQ_REMOVE(&limits, l, entries);
		free(l);
	}
	divecmd_free(&dq, &st);
	return rc ? EXIT_SUCCESS : EXIT_FAILURE;

usage:
	while (NULL != (l = TAILQ_FIRST(&limits))) {
		TAILQ_REMOVE(&limits, l, entries);
		free(l);
	}
	fprintf(stderr, "usage: %s "
		"[-v] [-l limit] "
		"[file ...]\n", getprogname());
	return EXIT_FAILURE;
}
