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
#include <sys/queue.h>

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
	struct diveq	*dives; /* all dives */
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
}

static void
logerr(const struct parse *p)
{

	logerrx(p, "%s\n", XML_ErrorString(XML_GetErrorCode(p->p)));
}

static void
parse_open(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct parse	 *p = dat;
	struct samp	 *samp;
	struct dive	 *dive;
	const XML_Char	**attp;

	if (0 == strcmp(s, "dive")) {
		p->curdive = dive = 
			calloc(1, sizeof(struct dive));
		if (NULL == dive)
			err(EXIT_FAILURE, NULL);
		if (verbose)
			fprintf(stderr, "%s: new dive: %zu\n",
				p->file, dive->num);
		TAILQ_INSERT_TAIL(p->dives, dive, entries);
		TAILQ_INIT(&dive->samps);
		for (attp = atts; NULL != *attp; attp += 2) {
			if (0 == strcmp(attp[0], "number")) {
				dive->num = atoi(attp[1]);
			} else if (0 == strcmp(attp[0], "date")) {
				free(dive->date);
				dive->date = strdup(attp[1]);
				if (NULL == dive->date)
					err(EXIT_FAILURE, NULL);
			} else if (0 == strcmp(attp[0], "time")) {
				free(dive->time);
				dive->time = strdup(attp[1]);
				if (NULL == dive->time)
					err(EXIT_FAILURE, NULL);
			} else if (0 == strcmp(attp[0], "mode")) {
				if (0 == strcmp(attp[1], "freedive"))
					dive->mode = MODE_FREEDIVE;
				else if (0 == strcmp(attp[1], "opencircuit"))
					dive->mode = MODE_OC;
				else if (0 == strcmp(attp[1], "closedcircuit"))
					dive->mode = MODE_CC;
				else if (0 == strcmp(attp[1], "gauge"))
					dive->mode = MODE_GAUGE;
			}
		}
	} else if (0 == strcmp(s, "sample")) {
		if (NULL == (dive = p->curdive)) { 
			logerrx(p, "sample outside dive");
			return;
		}
		if (NULL == (samp = calloc(1, sizeof(struct samp))))
			err(EXIT_FAILURE, NULL);
		TAILQ_INSERT_TAIL(&dive->samps, samp, entries);
		dive->nsamps++;
		for (attp = atts; NULL != *attp; attp += 2) {
			if (0 == strcmp(attp[0], "time")) {
				samp->time = atoi(attp[1]);
				if (samp->time > dive->maxtime)
					dive->maxtime = samp->time;
			} else if (0 == strcmp(attp[0], "depth")) {
				samp->flags |= SAMP_DEPTH;
				samp->depth = atof(attp[1]);
				if (samp->depth > dive->maxdepth)
					dive->maxdepth = samp->depth;
			}
		}
		if (verbose)
			fprintf(stderr, "%s: new sample: %zu, %zu\n", 
				p->file, dive->num, samp->time);
	}
}

static void
parse_close(void *dat, const XML_Char *s)
{
	struct parse	*p = dat;

	if (0 == strcmp(s, "dive"))
		p->curdive = NULL;
}

int
parse(const char *fname, XML_Parser p, struct diveq *dq)
{
	int	 	 fd;
	struct parse	 pp;
	ssize_t		 ssz;
	char		 buf[BUFSIZ];
	enum XML_Status	 st;

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

	XML_ParserReset(p, NULL);
	XML_SetElementHandler(p, parse_open, parse_close);
	XML_SetUserData(p, &pp);

	while ((ssz = read(fd, buf, sizeof(buf))) > 0) {
	       st = XML_Parse(p, buf, (int)ssz, 0 == ssz ? 1 : 0);
	       if (XML_STATUS_OK != st) {
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
		free(d->date);
		free(d->time);
		free(d);
	}
}
