/*	$Id$ */
/*
 * Copyright (c) 2016--2017 Kristaps Dzonsons <kristaps@bsd.lv>,
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
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <expat.h>

#include "parser.h"

struct	parse {
	XML_Parser	 p; /* parser routine */
	const char	*file; /* parsed filename */
	struct dive	*curdive; /* current dive */
	struct samp	*cursamp; /* current sample */
	struct diveq	*dives; /* all dives */
	struct divestat	*stat; /* statistics */
};

static void
logerrx(const struct parse *p, const char *fmt, ...)
{
	va_list	 ap;

	fprintf(stderr, "%s:%zu:%zu: ", p->file,
		XML_GetCurrentLineNumber(p->p),
		XML_GetCurrentColumnNumber(p->p));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static void
logerr(const struct parse *p)
{

	logerrx(p, "%s", XML_ErrorString(XML_GetErrorCode(p->p)));
}

static void
parse_open(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct parse	 *p = dat;
	struct samp	 *samp;
	struct dive	 *dive, *dp;
	const XML_Char	**attp;
	const char	 *date, *time;
	struct tm	  tm;
	int		  rc;

	if (0 == strcmp(s, "dive")) {
		if (NULL != p->cursamp) {
			logerrx(p, "dive within sample");
			return;
		} else if (NULL != p->curdive) {
			logerrx(p, "recursive dive");
			return;
		}

		/*
		 * We're encountering a new dive.
		 * Allocate it and zero all values.
		 * Then walk through the attributes to grab everything
		 * that's important to us.
		 */

		p->curdive = dive = 
			calloc(1, sizeof(struct dive));
		if (NULL == dive)
			err(EXIT_FAILURE, NULL);
		if (verbose)
			fprintf(stderr, "%s: new dive: %zu\n",
				p->file, dive->num);
		TAILQ_INIT(&dive->samps);

		date = time = NULL;
		for (attp = atts; NULL != *attp; attp += 2) {
			/* FIXME: use strtonum. */
			if (0 == strcmp(attp[0], "number")) {
				dive->num = atoi(attp[1]);
			} else if (0 == strcmp(attp[0], "date")) {
				date = attp[1];
			} else if (0 == strcmp(attp[0], "time")) {
				time = attp[1];
			} else if (0 == strcmp(attp[0], "mode")) {
				if (0 == strcmp(attp[1], "freedive"))
					dive->mode = MODE_FREEDIVE;
				else if (0 == strcmp(attp[1], "opencircuit"))
					dive->mode = MODE_OC;
				else if (0 == strcmp(attp[1], "closedcircuit"))
					dive->mode = MODE_CC;
				else if (0 == strcmp(attp[1], "gauge"))
					dive->mode = MODE_GAUGE;
				else
					logerrx(p, "unknown mode");
			}
		}

		/* Attempt to convert the date and time. */

		if (NULL != date && NULL != time) {
			memset(&tm, 0, sizeof(struct tm));
			rc = sscanf(date, "%d-%d-%d", 
				&tm.tm_year, &tm.tm_mon, &tm.tm_mday);
			if (3 != rc) {
				logerrx(p, "malformed date");
				TAILQ_INSERT_TAIL(p->dives, dive, entries);
				return;
			}
			tm.tm_year -= 1900;
			tm.tm_mon -= 1;
			rc = sscanf(time, "%d:%d:%d", 
				&tm.tm_hour, &tm.tm_min, &tm.tm_sec);
			if (3 != rc) {
				logerrx(p, "malformed time");
				TAILQ_INSERT_TAIL(p->dives, dive, entries);
				return;
			}
			tm.tm_isdst = -1;
			if (-1 == (dive->datetime = mktime(&tm))) {
				logerrx(p, "malformed date-time");
				dive->datetime = 0;
				TAILQ_INSERT_TAIL(p->dives, dive, entries);
				return;
			}
			if (0 == p->stat->timestamp_min ||
			    dive->datetime < p->stat->timestamp_min)
				p->stat->timestamp_min = dive->datetime;
			if (0 == p->stat->timestamp_max ||
			    dive->datetime > p->stat->timestamp_max)
				p->stat->timestamp_max = dive->datetime;

			/* Insert is in reverse order. */

			TAILQ_FOREACH(dp, p->dives, entries)
				if (dp->datetime &&
				    dp->datetime > dive->datetime)
					break;

			if (NULL == dp)
				TAILQ_INSERT_TAIL(p->dives, dive, entries);
			else
				TAILQ_INSERT_BEFORE(dp, dive, entries);
		}

	} else if (0 == strcmp(s, "sample")) {
		if (NULL == (dive = p->curdive)) { 
			logerrx(p, "sample outside dive");
			return;
		}
		for (attp = atts; NULL != attp[0]; attp += 2)
			if (0 == strcmp(attp[0], "time"))
				break;
		if (NULL == attp[0]) {
			logerrx(p, "sample without time");
			return;
		}
		p->cursamp = samp = 
			calloc(1, sizeof(struct samp));
		if (NULL == samp)
			err(EXIT_FAILURE, NULL);
		samp->time = atoi(attp[1]);
		TAILQ_INSERT_TAIL(&dive->samps, samp, entries);
		dive->nsamps++;
		if (samp->time > dive->maxtime)
			dive->maxtime = samp->time;
		if (dive->datetime &&
		    dive->datetime + (time_t)samp->time > 
		    p->stat->timestamp_max)
			p->stat->timestamp_max =
				dive->datetime +
				(time_t)samp->time;
		if (verbose)
			fprintf(stderr, "%s: new sample: %zu, %zu\n", 
				p->file, dive->num, samp->time);
	} else if (0 == strcmp(s, "depth")) {
		if (NULL == (samp = p->cursamp))
			return;
		assert(NULL != p->curdive);
		for (attp = atts; NULL != attp[0]; attp += 2)
			if (0 == strcmp(attp[0], "value")) 
				break;
		if (NULL == attp[0]) {
			logerrx(p, "depth without value");
			return;
		}
		samp->depth = atof(attp[1]);
		samp->flags |= SAMP_DEPTH;
		if (samp->depth > p->curdive->maxdepth)
			p->curdive->maxdepth = samp->depth;
	} else if (0 == strcmp(s, "temp")) {
		if (NULL == (samp = p->cursamp)) {
			logerrx(p, "temperature outside sample");
			return;
		}
		for (attp = atts; NULL != attp[0]; attp += 2)
			if (0 == strcmp(attp[0], "value")) 
				break;
		if (NULL == attp[0]) {
			logerrx(p, "temperature without value");
			return;
		}
		samp->temp = atof(attp[1]);
		samp->flags |= SAMP_TEMP;
	}
}

static void
parse_close(void *dat, const XML_Char *s)
{
	struct parse	*p = dat;

	if (0 == strcmp(s, "dive"))
		p->curdive = NULL;
	else if (0 == strcmp(s, "sample"))
		p->cursamp = NULL;
}

int
parse(const char *fname, XML_Parser p, 
	struct diveq *dq, struct divestat *st)
{
	int	 	 fd;
	struct parse	 pp;
	ssize_t		 ssz;
	char		 buf[BUFSIZ];
	enum XML_Status	 rc;

	fd = strcmp("-", fname) ? 
		open(fname, O_RDONLY, 0) : STDIN_FILENO;
	if (-1 == fd) {
		warn("%s", fname);
		return(0);
	}

	memset(&pp, 0, sizeof(struct parse));

	pp.file = STDIN_FILENO == fd ? "<stdin>" : fname;
	pp.p = p;
	pp.dives = dq;
	pp.stat = st;

	XML_ParserReset(p, NULL);
	XML_SetElementHandler(p, parse_open, parse_close);
	XML_SetUserData(p, &pp);

	while ((ssz = read(fd, buf, sizeof(buf))) > 0) {
	       rc = XML_Parse(p, buf, (int)ssz, 0 == ssz ? 1 : 0);
	       if (XML_STATUS_OK != rc) {
		       logerr(&pp);
		       break;
	       } else if (0 == ssz)
		       break;
	}

	if (ssz < 0)
		warn("%s", fname);

	close(fd);
	return(0 == ssz);
}

void
parse_free(struct diveq *dq)
{
	struct dive	*d;
	struct samp	*samp;

	if (NULL == dq)
		return;

	while ( ! TAILQ_EMPTY(dq)) {
		d = TAILQ_FIRST(dq);
		TAILQ_REMOVE(dq, d, entries);
		while ( ! TAILQ_EMPTY(&d->samps)) {
			samp = TAILQ_FIRST(&d->samps);
			TAILQ_REMOVE(&d->samps, samp, entries);
			free(samp);
		}
		free(d);
	}
}
