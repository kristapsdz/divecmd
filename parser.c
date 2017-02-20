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
	XML_Parser	  p; /* parser routine */
	const char	 *file; /* parsed filename */
	struct dlog	 *curlog; /* current divelog */
	struct dive	 *curdive; /* current dive */
	struct samp	 *cursamp; /* current sample */
	struct diveq	 *dives; /* all dives */
	struct divestat	 *stat; /* statistics */
};

/*
 * Generic logging function printing the given varargs message.
 */
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

/*
 * Generic logging function printing the current XML parse error code.
 */
static void
logerr(const struct parse *p)
{

	logerrx(p, "%s", XML_ErrorString(XML_GetErrorCode(p->p)));
}

/*
 * Add the dive "d" to the group "i" (which must exist) indexing in the
 * array of groups.
 * Returns the group (never NULL).
 * This inserts the groups in sorted error, if applicable.
 */
static struct dgroup *
group_add(struct parse *p, size_t i, struct dive *d)
{
	struct dive	*dp;
	struct dgroup	*dg = p->stat->groups[i];

	d->group = dg;
	dg->ndives++;

	/* If we have no date-time, just append. */

	if (0 == d->datetime) {
		TAILQ_INSERT_TAIL(&dg->dives, d, gentries);
		return(dg);
	}

	/* Otherwise, insert in date-sorted order. */

	TAILQ_FOREACH(dp, &dg->dives, gentries)
		if (dp->datetime &&
		    dp->datetime > d->datetime)
			break;

	if (NULL == dp)
		TAILQ_INSERT_TAIL(&dg->dives, d, gentries);
	else
		TAILQ_INSERT_BEFORE(dp, d, gentries);

	return(dg);
}

/*
 * Add a new group, optionally with date "date", to the set of groups.
 * This always returns.
 */
static struct dgroup *
group_alloc(struct parse *p, struct dive *d, const char *date)
{
	size_t	 i = p->stat->groupsz;

	p->stat->groupsz++;
	p->stat->groups = reallocarray
		(p->stat->groups, 
		 p->stat->groupsz, 
		 sizeof(struct dgroup *));
	if (NULL == p->stat->groups)
		err(EXIT_FAILURE, NULL);

	p->stat->groups[i] = calloc
		(1, sizeof(struct dgroup));
	if (NULL == p->stat->groups[i])
		err(EXIT_FAILURE, NULL);

	/* Name our group for lookup, if applicable. */

	if (NULL != date) {
		p->stat->groups[i]->name = strdup(date);
		if (NULL == p->stat->groups[i]->name)
			err(EXIT_FAILURE, NULL);
	}

	TAILQ_INIT(&p->stat->groups[i]->dives);
	p->stat->groups[i]->id = i;
	return(group_add(p, i, d));
}

/*
 * Get or create a group by the given "name", which might be, say, the
 * date or the diver (it doesn't matter).
 * Returns the created or augmented group.
 */
static struct dgroup *
group_lookup(struct parse *p, struct dive *d, const char *name)
{
	size_t	 i;

	for (i = 0; i < p->stat->groupsz; i++)
		if (0 == strcmp(name, p->stat->groups[i]->name))
			break;
	if (i == p->stat->groupsz && verbose)
		fprintf(stderr, "%s: new group: %s\n", p->file, name);

	return((i == p->stat->groupsz) ?
		group_alloc(p, d, name) :
		group_add(p, i, d));
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
	struct dgroup	 *grp;

	if (0 == strcmp(s, "divelog")) {
		/*
		 * Top-level node of a dive.
		 * This contains (optional) information pertaining to
		 * the dive computer that recorded the dives.
		 */

		if (NULL != p->curlog) {
			logerrx(p, "recursive divelog");
			return;
		}
		p->curlog = calloc(1, sizeof(struct dlog));
		if (NULL == p->curlog)
			err(EXIT_FAILURE, NULL);

		/* Grok the optional diver identifier. */

		for (attp = atts; NULL != *attp; attp += 2)
			if (0 == strcmp(attp[0], "diver")) {
				free(p->curlog->ident);
				p->curlog->ident = strdup(attp[1]);
				if (NULL == p->curlog->ident)
					err(EXIT_FAILURE, NULL);
			}

	} else if (0 == strcmp(s, "dive")) {
		/*
		 * We're encountering a new dive.
		 * Allocate it and zero all values.
		 * Then walk through the attributes to grab everything
		 * that's important to us.
		 */

		if (NULL != p->cursamp) {
			logerrx(p, "dive within sample");
			return;
		} else if (NULL != p->curdive) {
			logerrx(p, "recursive dive");
			return;
		} else if (NULL == p->curlog) {
			logerrx(p, "missing divelog");
			return;
		}

		p->curdive = dive = 
			calloc(1, sizeof(struct dive));
		if (NULL == dive)
			err(EXIT_FAILURE, NULL);
		TAILQ_INIT(&dive->samps);
		dive->log = p->curlog;

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

		if (verbose)
			fprintf(stderr, "%s: new dive: %zu\n",
				p->file, dive->num);

		/* 
		 * Now register the dive with the dive queue.
		 * If the dive has a date and time, we order everything
		 * by time.
		 * If not, we just append to the queue.
		 * If we have group sorting, drop any dives without a
		 * date and time.
		 */

		if (NULL != date && NULL != time) {
			/* We now perform our date conversion. */

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

			/* Convert raw values into epoch. */

			if (-1 == (dive->datetime = mktime(&tm))) {
				logerrx(p, "malformed date-time");
				dive->datetime = 0;
				TAILQ_INSERT_TAIL(p->dives, dive, entries);
				return;
			}

			/* Check against our global extrema. */

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
		} else {
			/*
			 * Un-timedated dive goes wherever.
			 */
			TAILQ_INSERT_TAIL(p->dives, dive, entries);
		}

		/*
		 * Now assign to our group.
		 * Our group assignment might require data we don't
		 * have, so we might not be grouping the dive.
		 */

		if (GROUP_DATE == p->stat->group) {
			if (NULL == date) {
				logerrx(p, "group dive without date");
				grp = group_lookup(p, dive, "");
			} else
				grp = group_lookup(p, dive, date);
		} else if (GROUP_DIVER == p->stat->group) {
			if (NULL == dive->log->ident) {
				logerrx(p, "group dive without diver");
				grp = group_lookup(p, dive, "");
			} else
				grp = group_lookup(p, 
					dive, dive->log->ident);
		} else {
			if (0 == p->stat->groupsz && verbose)
				fprintf(stderr, "%s: new default "
					"group\n", p->file);
			grp = (0 == p->stat->groupsz) ?
				group_alloc(p, dive, NULL) :
				group_add(p, 0, dive);
		}

		if (NULL != date && 
		    (0 == grp->mintime || dive->datetime < grp->mintime))
			grp->mintime = dive->datetime;
	} else if (0 == strcmp(s, "sample")) {
		/*
		 * Add a diving sample.
		 * This must be connected to the current dive.
		 */
		if (NULL == (dive = p->curdive)) { 
			logerrx(p, "sample outside dive");
			return;
		}

		/* The sample must have a time attribute. */

		for (attp = atts; NULL != attp[0]; attp += 2)
			if (0 == strcmp(attp[0], "time"))
				break;
		if (NULL == attp[0]) {
			logerrx(p, "sample without time");
			return;
		}

		/* Initialise current sample. */

		p->cursamp = samp = 
			calloc(1, sizeof(struct samp));
		if (NULL == samp)
			err(EXIT_FAILURE, NULL);

		/* XXX: use strtonum. */
		samp->time = atoi(attp[1]);

		TAILQ_INSERT_TAIL(&dive->samps, samp, entries);
		dive->nsamps++;

		/* Adjust dive extrema. */

		if (samp->time > dive->maxtime)
			dive->maxtime = samp->time;
		if (dive->datetime &&
		    dive->datetime + (time_t)samp->time > 
		    p->stat->timestamp_max)
			p->stat->timestamp_max =
				dive->datetime +
				(time_t)samp->time;

		if (verbose > 1)
			fprintf(stderr, "%s: new sample: %zu, %zu\n", 
				p->file, dive->num, samp->time);
	} else if (0 == strcmp(s, "depth")) {
		/*
		 * Add the depth to a current diving sample.
		 * Obviously the sample must be available.
		 * XXX: we might have a <depth> outside of the dive, so
		 * don't print an error.
		 */
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

		/* FIXME: use strtonu---whatever. */

		samp->depth = atof(attp[1]);
		samp->flags |= SAMP_DEPTH;

		/* Adjust extrema. */

		if (samp->depth > p->curdive->maxdepth)
			p->curdive->maxdepth = samp->depth;
	} else if (0 == strcmp(s, "temp")) {
		/*
		 * Add a temperature to the current sample.
		 * The sample must be available.
		 */
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

	if (0 == strcmp(s, "divelog"))
		p->curlog = NULL;
	else if (0 == strcmp(s, "dive"))
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
parse_free(struct diveq *dq, struct divestat *st)
{
	struct dive	*d;
	struct dlog	*dl;
	struct samp	*samp;
	size_t		 i;

	if (NULL != dq)
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

	if (NULL != st) {
		for (i = 0; i < st->groupsz; i++) {
			free(st->groups[i]->name);
			free(st->groups[i]);
		}
		free(st->groups);
		while ( ! TAILQ_EMPTY(&st->dlogs)) {
			dl = TAILQ_FIRST(&st->dlogs);
			TAILQ_REMOVE(&st->dlogs, dl, entries);
			free(dl->ident);
			free(dl);
		}
	}
}

void
parse_init(XML_Parser *p, struct diveq *dq, 
	struct divestat *st, enum group group)
{

	if (NULL == (*p = XML_ParserCreate(NULL)))
		err(EXIT_FAILURE, NULL);

	TAILQ_INIT(dq);
	memset(st, 0, sizeof(struct divestat));
	st->group = group;
	TAILQ_INIT(&st->dlogs);
}
