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
#include "config.h"
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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
	char		 *buf; /* temporary buffer */
	size_t		  bufsz; /* length of buf */
};

static void
logdbg(const struct parse *p, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

static void
logwarnx(const struct parse *p, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

static void
logerrx(const struct parse *p, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

static __dead void
logfatal(const struct parse *p, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

static void
logerrx(const struct parse *p, const char *fmt, ...)
{
	va_list	 ap;

	fprintf(stderr, "%s:%zu:%zu: error: ", p->file,
		XML_GetCurrentLineNumber(p->p),
		XML_GetCurrentColumnNumber(p->p));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static void
logwarnx(const struct parse *p, const char *fmt, ...)
{
	va_list	 ap;

	fprintf(stderr, "%s:%zu:%zu: warning: ", p->file,
		XML_GetCurrentLineNumber(p->p),
		XML_GetCurrentColumnNumber(p->p));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static void
logdbg(const struct parse *p, const char *fmt, ...)
{
	va_list	 ap;

	if ( ! verbose)
		return;

	fprintf(stderr, "%s:%zu:%zu: ", p->file,
		XML_GetCurrentLineNumber(p->p),
		XML_GetCurrentColumnNumber(p->p));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static void
logfatal(const struct parse *p, const char *fmt, ...)
{
	va_list	 ap;
	int	 er = errno;

	fprintf(stderr, "%s:%zu:%zu: fatal: ", p->file,
		XML_GetCurrentLineNumber(p->p),
		XML_GetCurrentColumnNumber(p->p));

	if (NULL != fmt) {
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
		fprintf(stderr, ": ");
	}

	fprintf(stderr, "%s\n", strerror(er));
	exit(EXIT_FAILURE);
}

static char *
xstrndup(const struct parse *p, const char *cp, size_t sz)
{
	char	*pp;

	if (NULL == (pp = strndup(cp, sz)))
		logfatal(p, "strndup");
	return(pp);
}

static char *
xstrdup(const struct parse *p, const char *cp)
{
	char	*pp;

	if (NULL == (pp = strdup(cp)))
		logfatal(p, "strdup");
	return(pp);
}

static void
logerrp(const struct parse *p)
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

	if (NULL != date)
		p->stat->groups[i]->name = xstrdup(p, date);

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
parse_text(void *dat, const XML_Char *s, int len)
{
	struct parse	*p = dat;

	if (0 == len)
		return;
	p->buf = realloc(p->buf, p->bufsz + len);
	if (NULL == p->buf)
		logfatal(p, "realloc");
	memcpy(p->buf + p->bufsz, s, len);
	p->bufsz += len;
}

static void
parse_open(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct parse	 *p = dat;
	struct samp	 *samp;
	struct dive	 *dive, *dp;
	const XML_Char	**attp;
	const char	 *date, *time, *num, *er, *dur, *mode;
	char		 *ep;
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
			logwarnx(p, "recursive divelog");
			return;
		}
		p->curlog = calloc(1, sizeof(struct dlog));
		if (NULL == p->curlog)
			err(EXIT_FAILURE, NULL);

		/* Grok the optional diver identifier. */

		for (attp = atts; NULL != *attp; attp += 2)
			if (0 == strcmp(attp[0], "diver")) {
				free(p->curlog->ident);
				p->curlog->ident = xstrdup(p, attp[1]);
			} else if (0 == strcmp(attp[0], "vendor")) {
				free(p->curlog->vendor);
				p->curlog->vendor = xstrdup(p, attp[1]);
			} else if (0 == strcmp(attp[0], "product")) {
				free(p->curlog->product);
				p->curlog->product = xstrdup(p, attp[1]);
			} else if (0 == strcmp(attp[0], "model")) {
				free(p->curlog->model);
				p->curlog->model = xstrdup(p, attp[1]);
			} else if (0 == strcmp(attp[0], "program")) {
				free(p->curlog->program);
				p->curlog->program = xstrdup(p, attp[1]);
			}
		TAILQ_INSERT_TAIL(&p->stat->dlogs, p->curlog, entries);
		logdbg(p, "new divelog");
	} else if (0 == strcmp(s, "dive")) {
		/*
		 * We're encountering a new dive.
		 * Allocate it and zero all values.
		 * Then walk through the attributes to grab everything
		 * that's important to us.
		 */

		if (NULL != p->cursamp) {
			logwarnx(p, "dive within sample");
			return;
		} else if (NULL != p->curdive) {
			logwarnx(p, "recursive dive");
			return;
		} else if (NULL == p->curlog) {
			logwarnx(p, "missing divelog");
			return;
		}

		p->curdive = dive = 
			calloc(1, sizeof(struct dive));
		if (NULL == dive)
			err(EXIT_FAILURE, NULL);
		TAILQ_INIT(&dive->samps);
		dive->log = p->curlog;

		date = time = num = dur = mode = NULL;

		for (attp = atts; NULL != *attp; attp += 2) {
			if (0 == strcmp(attp[0], "number"))
				num = attp[1];
			else if (0 == strcmp(attp[0], "duration"))
				dur = attp[1];
			else if (0 == strcmp(attp[0], "date"))
				date = attp[1];
			else if (0 == strcmp(attp[0], "time"))
				time = attp[1];
			else if (0 == strcmp(attp[0], "mode"))
				mode = attp[1];
		}

		if (NULL != mode) {
			if (0 == strcmp(mode, "freedive"))
				dive->mode = MODE_FREEDIVE;
			else if (0 == strcmp(mode, "opencircuit"))
				dive->mode = MODE_OC;
			else if (0 == strcmp(mode, "closedcircuit"))
				dive->mode = MODE_CC;
			else if (0 == strcmp(mode, "gauge"))
				dive->mode = MODE_GAUGE;
			else
				logwarnx(p, "dive mode unknown");
		}

		if (NULL != num) {
			dive->num = strtonum
				(num, 0, LONG_MAX, &er);
			if (NULL != er) {
				logwarnx(p, "dive number is %s", er);
				logdbg(p, "new dive: <unnumbered>");
			} else
				logdbg(p, "new dive: %zu", dive->num);
		}

		if (NULL != dur) {
			dive->duration = strtonum
				(dur, 0, LONG_MAX, &er);
			if (NULL != er)
				logwarnx(p, "dive duration is %s", er);
		}

		/* Configure date and time. */

		if (NULL != date && NULL != time) {
			memset(&tm, 0, sizeof(struct tm));

			rc = sscanf(date, "%d-%d-%d", 
				&tm.tm_year, &tm.tm_mon, &tm.tm_mday);
			if (3 != rc) {
				logwarnx(p, "malformed date");
				TAILQ_INSERT_TAIL(p->dives, dive, entries);
				return;
			}
			tm.tm_year -= 1900;
			tm.tm_mon -= 1;

			rc = sscanf(time, "%d:%d:%d", 
				&tm.tm_hour, &tm.tm_min, &tm.tm_sec);
			if (3 != rc) {
				logwarnx(p, "malformed time");
				TAILQ_INSERT_TAIL(p->dives, dive, entries);
				return;
			}
			tm.tm_isdst = -1;

			/* Convert raw values into epoch. */

			if (-1 == (dive->datetime = mktime(&tm))) {
				logwarnx(p, "malformed date-time");
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
		} 

		/*
		 * Now assign to our group.
		 * Our group assignment might require data we don't
		 * have, so we might not be grouping the dive.
		 */

		if (GROUP_DATE == p->stat->group) {
			if (NULL == date) {
				logwarnx(p, "group dive without date");
				grp = group_lookup(p, dive, "");
			} else
				grp = group_lookup(p, dive, date);
		} else if (GROUP_DIVER == p->stat->group) {
			if (NULL == dive->log->ident) {
				logwarnx(p, "group dive without diver");
				grp = group_lookup(p, dive, "");
			} else
				grp = group_lookup(p, 
					dive, dive->log->ident);
		} else {
			if (0 == p->stat->groupsz && verbose)
				logdbg(p, "new default group");
			grp = (0 == p->stat->groupsz) ?
				group_alloc(p, dive, NULL) :
				group_add(p, 0, dive);
		}

		if (NULL != date && 
		    (0 == grp->mintime || dive->datetime < grp->mintime))
			grp->mintime = dive->datetime;

		/* 
		 * Now register the dive with the dive queue.
		 * If the dive has a date and time, we order everything
		 * by time; and furthermore, we order it by offsetting
		 * from our group's start time.
		 * If there's no date/time, we just append to the queue.
		 */

		if (NULL != date && NULL != time) {
			/* 
			 * Insert is in reverse order. 
			 */
			TAILQ_FOREACH(dp, p->dives, entries)
				if (dp->datetime - dp->group->mintime &&
				    dp->datetime - dp->group->mintime > 
				    dive->datetime - dive->group->mintime)
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
	} else if (0 == strcmp(s, "fingerprint")) {
		/*
		 * Start recording the fingerprint.
		 * Don't allow us to be within a nested context.
		 */
		if (p->bufsz)
			logwarnx(p, "nested fingerprint");
		else
			XML_SetDefaultHandler(p->p, parse_text);
	} else if (0 == strcmp(s, "sample")) {
		/*
		 * Add a diving sample.
		 * This must be connected to the current dive.
		 */
		if (NULL == (dive = p->curdive)) { 
			logwarnx(p, "sample outside dive");
			return;
		}

		/* The sample must have a time attribute. */

		for (attp = atts; NULL != attp[0]; attp += 2)
			if (0 == strcmp(attp[0], "time"))
				break;
		if (NULL == attp[0]) {
			logwarnx(p, "sample without time");
			return;
		}

		/* Initialise current sample. */

		p->cursamp = samp = 
			calloc(1, sizeof(struct samp));
		if (NULL == samp)
			err(EXIT_FAILURE, NULL);

		/* XXX: the size of time_t isn't portable. */

		samp->time = strtonum(attp[1], 0, LONG_MAX, &er);
		if (NULL != er)
			logwarnx(p, "sample time is %s", er);

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
		if (NULL == (samp = p->cursamp))
			return;
		assert(NULL != p->curdive);

		for (attp = atts; NULL != attp[0]; attp += 2)
			if (0 == strcmp(attp[0], "value")) 
				break;

		if (NULL == attp[0]) {
			logwarnx(p, "sample depth without value");
			return;
		} 

		samp->depth = strtod(attp[1], &ep);
		if ( ! (ep == attp[1] || ERANGE == errno)) {
			samp->flags |= SAMP_DEPTH;
			if (samp->depth > p->curdive->maxdepth)
				p->curdive->maxdepth = samp->depth;
		} else
			logwarnx(p, "sample depth malformed");
	} else if (0 == strcmp(s, "rbt")) {
		if (NULL == (samp = p->cursamp)) {
			logwarnx(p, "sample rbt outside sample");
			return;
		}

		for (attp = atts; NULL != attp[0]; attp += 2)
			if (0 == strcmp(attp[0], "value")) 
				break;

		if (NULL == attp[0]) {
			logwarnx(p, "sample rbt without value");
			return;
		}

		samp->rbt = strtonum(attp[1], 0, LONG_MAX, &er);
		if (NULL != er)
			logwarnx(p, "sample rbt is %s", er);
		else
			samp->flags |= SAMP_RBT;
	} else if (0 == strcmp(s, "temp")) {
		if (NULL == (samp = p->cursamp)) {
			logwarnx(p, "sample temp outside sample");
			return;
		}

		for (attp = atts; NULL != attp[0]; attp += 2)
			if (0 == strcmp(attp[0], "value")) 
				break;

		if (NULL == attp[0]) {
			logwarnx(p, "sample temp without value");
			return;
		}

		samp->temp = strtod(attp[1], &ep);
		if ( ! (ep == attp[1] || ERANGE == errno)) {
			samp->flags |= SAMP_TEMP;
			if (0 == p->curdive->hastemp) {
				p->curdive->maxtemp = samp->temp;
				p->curdive->mintemp = samp->temp;
				p->curdive->hastemp = 1;
			} else {
				if (samp->temp > p->curdive->maxtemp)
					p->curdive->maxtemp = samp->temp;
				if (samp->temp < p->curdive->mintemp)
					p->curdive->mintemp = samp->temp;
			}
		} else
			logwarnx(p, "sample temp malformed");
	}
}

static void
parse_close(void *dat, const XML_Char *s)
{
	struct parse	*p = dat;

	if (0 == strcmp(s, "fingerprint")) {
		/*
		 * Set the fingerprint.
		 * An empty element unsets the fingerprint.
		 * We must be within a dive context.
		 */

		XML_SetDefaultHandler(p->p, NULL);
		if (NULL != p->curdive && p->bufsz) {
			free(p->curdive->fprint);
			p->curdive->fprint = 
				xstrndup(p, p->buf, p->bufsz);
		} else if (NULL != p->curdive && 0 == p->bufsz) {
			free(p->curdive->fprint);
			p->curdive->fprint = NULL;
		} else
			logwarnx(p, "fingerprint not in dive context");
		free(p->buf);
		p->buf = NULL;
		p->bufsz = 0;
	} else if (0 == strcmp(s, "divelog")) {
		p->curlog = NULL;
	} else if (0 == strcmp(s, "dive")) {
		p->curdive = NULL;
	} else if (0 == strcmp(s, "sample")) {
		p->cursamp = NULL;
	}

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
		       logerrp(&pp);
		       break;
	       } else if (0 == ssz)
		       break;
	}

	if (ssz < 0)
		warn("%s", fname);

	close(fd);
	free(pp.buf);
	return(0 == ssz);
}

void
parse_free(struct diveq *dq, struct divestat *st)
{
	struct dive	*d;
	struct dlog	*dl;
	struct samp	*s;
	size_t		 i;

	if (NULL != dq)
		while (NULL != (d = TAILQ_FIRST(dq))) {
			TAILQ_REMOVE(dq, d, entries);
			while (NULL != (s = TAILQ_FIRST(&d->samps))) {
				TAILQ_REMOVE(&d->samps, s, entries);
				free(s);
			}
			free(d->fprint);
			free(d);
		}

	if (NULL != st) {
		for (i = 0; i < st->groupsz; i++) {
			free(st->groups[i]->name);
			free(st->groups[i]);
		}
		free(st->groups);
		while (NULL != (dl = TAILQ_FIRST(&st->dlogs))) {
			TAILQ_REMOVE(&st->dlogs, dl, entries);
			free(dl->product);
			free(dl->vendor);
			free(dl->model);
			free(dl->program);
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
