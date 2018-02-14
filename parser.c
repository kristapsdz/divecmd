/*	$Id$ */
/*
 * Copyright (c) 2016--2018 Kristaps Dzonsons <kristaps@bsd.lv>
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

static	const char *decos[DECO__MAX] = {
	"ndl", /* DECO_ndl */
	"safetystop", /* DECO_safetystop */
	"decostop", /* DECO_decostop */
	"deepstop", /* DECO_deepstop */
};

static	const char *events[EVENT__MAX] = {
	"none", /* EVENT_none */
	"decostop", /* EVENT_decostop */
	"rbt", /* EVENT_rbt */
	"ascent", /* EVENT_ascent */
	"ceiling", /* EVENT_ceiling */
	"workload", /* EVENT_workload */
	"transmitter", /* EVENT_transmitter */
	"violation", /* EVENT_violation */
	"bookmark", /* EVENT_bookmark */
	"surface", /* EVENT_surface */
	"safetystop", /* EVENT_safetystop */
	"gaschange", /* EVENT_gaschange */
	"safetystop_voluntary", /* EVENT_safetystop_voluntary */
	"safetystop_mandatory", /* EVENT_safetystop_mandatory */
	"deepstop", /* EVENT_deepstop */
	"ceiling_safetystop", /* EVENT_ceiling_safetystop */
	"floor", /* EVENT_floor */
	"divetime", /* EVENT_divetime */
	"maxdepth", /* EVENT_maxdepth */
	"olf", /* EVENT_olf */
	"po2", /* EVENT_po2 */
	"airtime", /* EVENT_airtime */
	"rgbm", /* EVENT_rgbm */
	"heading", /* EVENT_heading */
	"tissuelevel", /* EVENT_tissuelevel */
	"gaschange2", /* EVENT_gaschange2 */
};

static void
logdbg(const struct parse *p, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

static void
logwarnx(const struct parse *p, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

static void
logerrx(struct parse *p, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

static __dead void
logfatal(const struct parse *p, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

static void
logerrx(struct parse *p, const char *fmt, ...)
{
	va_list	 ap;

	fprintf(stderr, "%s:%zu:%zu: error: ", p->file,
		XML_GetCurrentLineNumber(p->p),
		XML_GetCurrentColumnNumber(p->p));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	XML_StopParser(p->p, 0);
}

static void
logattr(const struct parse *p, const char *tag, const char *attr)
{

	logwarnx(p, "%s: unknown <%s> attribute", attr, tag);
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

static void *
xreallocarray(const struct parse *p, void *ptr, size_t nm, size_t sz)
{

	if (NULL == (ptr = reallocarray(ptr, nm, sz)))
		logfatal(p, "reallocarray");
	return(ptr);
}

static void *
xcalloc(const struct parse *p, size_t nm, size_t sz)
{
	char	*pp;

	if (NULL == (pp = calloc(nm, sz)))
		logfatal(p, "calloc");
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
logerrp(struct parse *p)
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
	p->stat->groups = xreallocarray
		(p, p->stat->groups, 
		 p->stat->groupsz, 
		 sizeof(struct dgroup *));
	p->stat->groups[i] = xcalloc
		(p, 1, sizeof(struct dgroup));

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
	const XML_Char	**ap;
	struct parse	 *p = dat;
	struct samp	 *samp;
	struct dive	 *d, *dp;
	const char	 *date, *time, *num, *er, *dur, *mode, *v,
	       	  	 *mixes[3];
	char		 *ep;
	struct tm	  tm;
	int		  rc;
	struct dgroup	 *grp;
	size_t		  i;
	unsigned int	  bits;

	if (0 == strcmp(s, "divelog")) {
		if (NULL != p->curlog) {
			logerrx(p, "nested <divelog>");
			return;
		}

		p->curlog = xcalloc(p, 1, sizeof(struct dlog));
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "diver")) {
				free(p->curlog->ident);
				p->curlog->ident = xstrdup(p, ap[1]);
			} else if (0 == strcmp(*ap, "vendor")) {
				free(p->curlog->vendor);
				p->curlog->vendor = xstrdup(p, ap[1]);
			} else if (0 == strcmp(*ap, "product")) {
				free(p->curlog->product);
				p->curlog->product = xstrdup(p, ap[1]);
			} else if (0 == strcmp(*ap, "model")) {
				free(p->curlog->model);
				p->curlog->model = xstrdup(p, ap[1]);
			} else if (0 == strcmp(*ap, "program")) {
				free(p->curlog->program);
				p->curlog->program = xstrdup(p, ap[1]);
			} else if (0 != strcmp(*ap, "version"))
				logattr(p, "divelog", *ap);

		TAILQ_INSERT_TAIL(&p->stat->dlogs, p->curlog, entries);
		logdbg(p, "new divelog");
	} else if (0 == strcmp(s, "dive")) {
		if (NULL != p->cursamp) {
			logerrx(p, "<dive> within <sample>");
			return;
		} else if (NULL != p->curdive) {
			logerrx(p, "nested <dive>");
			return;
		} else if (NULL == p->curlog) {
			logerrx(p, "<dive> not in <divelog>");
			return;
		}

		p->curdive = d = xcalloc(p, 1, sizeof(struct dive));
		TAILQ_INIT(&d->samps);
		d->log = p->curlog;

		num = dur = date = time = mode = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "number"))
				num = ap[1];
			else if (0 == strcmp(*ap, "duration"))
				dur = ap[1];
			else if (0 == strcmp(*ap, "date"))
				date = ap[1];
			else if (0 == strcmp(*ap, "time"))
				time = ap[1];
			else if (0 == strcmp(*ap, "mode"))
				mode = ap[1];
			else 
				logattr(p, "dive", *ap);

		if (NULL != mode) {
			if (0 == strcmp(mode, "freedive"))
				d->mode = MODE_FREEDIVE;
			else if (0 == strcmp(mode, "opencircuit"))
				d->mode = MODE_OC;
			else if (0 == strcmp(mode, "closedcircuit"))
				d->mode = MODE_CC;
			else if (0 == strcmp(mode, "gauge"))
				d->mode = MODE_GAUGE;
			else
				logwarnx(p, "%s: unknown "
					"<dive> mode", mode);
		}

		if (NULL != num) {
			d->num = strtonum(num, 0, LONG_MAX, &er);
			if (NULL != er) {
				logwarnx(p, "malformed "
					"<dive> number: %s", er);
				logdbg(p, "new dive: <unnumbered>");
			} else
				logdbg(p, "new dive: %zu", d->num);
		}

		if (NULL != dur) {
			d->duration = strtonum(dur, 0, LONG_MAX, &er);
			if (NULL != er)
				logwarnx(p, "dive duration: %s", er);
		}

		if (NULL != date && NULL != time) {
			memset(&tm, 0, sizeof(struct tm));
			rc = sscanf(date, "%d-%d-%d", 
				&tm.tm_year, &tm.tm_mon, &tm.tm_mday);
			if (3 != rc) {
				logwarnx(p, "malformed "
					"<dive> date: %s", date);
				TAILQ_INSERT_TAIL(p->dives, d, entries);
				return;
			}
			tm.tm_year -= 1900;
			tm.tm_mon -= 1;

			rc = sscanf(time, "%d:%d:%d", 
				&tm.tm_hour, &tm.tm_min, &tm.tm_sec);
			if (3 != rc) {
				logwarnx(p, "malformed "
					"<dive> time: %s", time);
				TAILQ_INSERT_TAIL(p->dives, d, entries);
				return;
			}
			tm.tm_isdst = -1;

			/* Convert raw values into epoch. */

			if (-1 == (d->datetime = mktime(&tm))) {
				logwarnx(p, "malformed <dive> "
					"datetime: %s-%s", date, time);
				d->datetime = 0;
				TAILQ_INSERT_TAIL(p->dives, d, entries);
				return;
			}

			/* Check against our global extrema. */

			if (0 == p->stat->timestamp_min ||
			    d->datetime < p->stat->timestamp_min)
				p->stat->timestamp_min = d->datetime;
			if (0 == p->stat->timestamp_max ||
			    d->datetime > p->stat->timestamp_max)
				p->stat->timestamp_max = d->datetime;
		} 

		/*
		 * Now assign to our group.
		 * Our group assignment might require data we don't
		 * have, so we might not be grouping the dive.
		 */

		if (GROUP_DATE == p->stat->group) {
			if (NULL == date) {
				logwarnx(p, "group "
					"<dive> without date");
				grp = group_lookup(p, d, "");
			} else
				grp = group_lookup(p, d, date);
		} else if (GROUP_DIVER == p->stat->group) {
			if (NULL == d->log->ident) {
				logwarnx(p, "group "
					"<dive> without diver");
				grp = group_lookup(p, d, "");
			} else
				grp = group_lookup(p, 
					d, d->log->ident);
		} else {
			if (0 == p->stat->groupsz && verbose)
				logdbg(p, "new default group");
			grp = (0 == p->stat->groupsz) ?
				group_alloc(p, d, NULL) :
				group_add(p, 0, d);
		}

		if (NULL != date && 
		    (0 == grp->mintime || d->datetime < grp->mintime))
			grp->mintime = d->datetime;

		/* 
		 * Now register the dive with the dive queue.
		 * If the dive has a date and time, we order everything
		 * by time; and furthermore, we order it by offsetting
		 * from our group's start time.
		 * If there's no date/time, we just append to the queue.
		 */

		if (NULL != date && NULL != time) {
			/* Insert is in reverse order. */
			TAILQ_FOREACH(dp, p->dives, entries)
				if (dp->datetime - dp->group->mintime &&
				    dp->datetime - dp->group->mintime > 
				    d->datetime - d->group->mintime)
					break;
			if (NULL == dp)
				TAILQ_INSERT_TAIL(p->dives, d, entries);
			else
				TAILQ_INSERT_BEFORE(dp, d, entries);
		} else {
			/* Un-timedated dive goes wherever. */
			TAILQ_INSERT_TAIL(p->dives, d, entries);
		}
	} else if (0 == strcmp(s, "fingerprint")) {
		if (p->bufsz)
			logerrx(p, "nested <fingerprint>");
		else
			XML_SetDefaultHandler(p->p, parse_text);
	} else if (0 == strcmp(s, "gasmix")) {
		if (NULL == (d = p->curdive)) { 
			logerrx(p, "<gasmix> not in <dive>");
			return;
		}

		v = mixes[0] = mixes[1] = mixes[2] = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "num"))
				v = ap[1];
			else if (0 == strcmp(*ap, "o2"))
				mixes[0] = ap[1];
			else if (0 == strcmp(*ap, "n2"))
				mixes[1] = ap[1];
			else if (0 == strcmp(*ap, "he"))
				mixes[2] = ap[1];
			else
				logattr(p, "gasmix", *ap);
		if (NULL == v) {
			logerrx(p, "<gasmix> missing attributes");
			return;
		}

		i = strtonum(v, 0, LONG_MAX, &er);
		if (NULL != er) {
			logerrx(p, "malformed <gasmix> num: %s", v);
			return;
		}

		d->gas = xreallocarray(p, d->gas, 
			d->gassz + 1, sizeof(struct divegas));
		d->gas[d->gassz].num = i;
		if (NULL != mixes[0]) {
			d->gas[d->gassz].o2 = strtod(mixes[0], &ep);
			if (ep == v || ERANGE == errno) {
				d->gas[d->gassz].o2 = 0.0;
				logwarnx(p, "malformed <o2> "
					"value: %s", mixes[0]);
			}
		}
		if (NULL != mixes[1]) {
			d->gas[d->gassz].n2 = strtod(mixes[1], &ep);
			if (ep == v || ERANGE == errno) {
				d->gas[d->gassz].n2 = 0.0;
				logwarnx(p, "malformed <n2> "
					"value: %s", mixes[1]);
			}
		}
		if (NULL != mixes[2]) {
			d->gas[d->gassz].he = strtod(mixes[2], &ep);
			if (ep == v || ERANGE == errno) {
				d->gas[d->gassz].he = 0.0;
				logwarnx(p, "malformed <he> "
					"value: %s", mixes[2]);
			}
		}
		d->gassz++;
	} else if (0 == strcmp(s, "sample")) {
		if (NULL == (d = p->curdive)) { 
			logerrx(p, "<sample> not in <dive>");
			return;
		}

		v = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "time"))
				v = ap[1];
			else
				logattr(p, "sample", *ap);

		if (NULL == v) {
			logerrx(p, "<sample> missing attributes");
			return;
		}

		i = strtonum(v, 0, LONG_MAX, &er);
		if (NULL != er) {
			logerrx(p, "malformed <sample> time: %s", er);
			return;
		}

		p->cursamp = samp = xcalloc(p, 1, sizeof(struct samp));
		TAILQ_INSERT_TAIL(&d->samps, samp, entries);
		d->nsamps++;

		samp->time = i;

		if (samp->time > d->maxtime)
			d->maxtime = samp->time;
		if (d->datetime &&
		    d->datetime + (time_t)samp->time > 
		    p->stat->timestamp_max)
			p->stat->timestamp_max =
				d->datetime +
				(time_t)samp->time;

		logdbg(p, "new sample: num=%zu, time=%zu",
			d->num, samp->time);
	} else if (0 == strcmp(s, "depth")) {
		if (NULL == (samp = p->cursamp))
			return;

		v = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "value"))
				v = ap[1];
			else
				logattr(p, "depth", *ap);

		if (NULL == v) {
			logerrx(p, "<depth> missing attributes");
			return;
		}

		samp->depth = strtod(v, &ep);
		if (ep == v || ERANGE == errno) {
			logerrx(p, "malformed <depth> value: %s", v);
			return;
		}

		if (samp->depth > p->curdive->maxdepth)
			p->curdive->maxdepth = samp->depth;
		samp->flags |= SAMP_DEPTH;
	} else if (0 == strcmp(s, "rbt")) {
		if (NULL == (samp = p->cursamp)) {
			logerrx(p, "sample rbt outside sample");
			return;
		}

		v = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "value"))
				v = ap[1];
			else
				logattr(p, "rbt", *ap);

		if (NULL == v) {
			logerrx(p, "<rbt> missing attributes");
			return;
		}

		samp->rbt = strtonum(v, 0, LONG_MAX, &er);
		if (NULL != er) {
			logerrx(p, "malformed <rbt> value: %s", v);
			return;
		}
		samp->flags |= SAMP_RBT;
	} else if (0 == strcmp(s, "event")) {
		if (NULL == (samp = p->cursamp)) {
			logerrx(p, "<event> not in <sample>");
			return;
		}

		v = dur = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "type"))
				v = ap[1];
			else if (0 == strcmp(*ap, "duration"))
				dur = ap[1];
			else
				logattr(p, "event", *ap);

		if (NULL == v || NULL == dur) {
			logerrx(p, "<event> missing attributes");
			return;
		}

		for (bits = 0, i = 0; i < EVENT__MAX; i++)
			if (0 == strcmp(v, events[i])) {
				bits = (1U << i);
				break;
			}
		if (EVENT__MAX == i) {
			logerrx(p, "malformed <event> type: %s", v);
			return;
		}

		i = strtonum(dur, 0, LONG_MAX, &er);
		if (NULL != er) {
			logerrx(p, "malformed <event> duration: %s", er);
			return;
		}

		samp->events = xreallocarray
			(p, samp->events, 
			 samp->eventsz + 1,
			 sizeof(struct sampevent));
		memset(&samp->events[samp->eventsz], 0,
			sizeof(struct sampevent));
		samp->events[samp->eventsz].bits = bits;
		samp->events[samp->eventsz].duration = i;
		samp->eventsz++;
		samp->flags |= SAMP_EVENT;
	} else if (0 == strcmp(s, "deco")) {
		if (NULL == (samp = p->cursamp)) {
			logerrx(p, "<deco> not in <sample>");
			return;
		}

		v = mode = dur = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2) 
			if (0 == strcmp(*ap, "depth"))
				v = ap[1];
			else if (0 == strcmp(*ap, "type"))
				mode = ap[1];
			else if (0 == strcmp(*ap, "duration"))
				dur = ap[1];
			else
				logattr(p, "deco", *ap);

		if (NULL == v || NULL == mode || NULL == dur) {
			logerrx(p, "<deco> missing attributes");
			return;
		}

		for (samp->deco.type = 0; 
		     samp->deco.type < DECO__MAX; 
		     samp->deco.type++)
			if (0 == strcmp(decos[samp->deco.type], mode))
				break;
		if (DECO__MAX == samp->deco.type) {
			logerrx(p, "unknown <deco> type: %s", mode);
			return;
		}

		samp->deco.depth = strtod(v, &ep);
		if (ep == v || ERANGE == errno) {
			logerrx(p, "malformed <deco> value: %s", v);
			return;
		}

		samp->deco.duration = strtonum(dur, 0, LONG_MAX, &er);
		if (NULL != er) {
			logerrx(p, "malformed <deco> duration: %s", dur);
			return;
		}

		samp->flags |= SAMP_DECO;
	} else if (0 == strcmp(s, "temp")) {
		if (NULL == (samp = p->cursamp)) {
			logerrx(p, "<temp> not in <sample>");
			return;
		}

		v = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "value"))
				v = ap[1];
			else
				logattr(p, "temp", *ap);

		if (NULL == v) {
			logerrx(p, "<temp> missing attributes");
			return;
		}

		samp->temp = strtod(v, &ep);
		if (ep == v || ERANGE == errno) {
			logerrx(p, "malformed <temp> value: %s", v);
			return;
		}

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
		samp->flags |= SAMP_TEMP;
	} else if (0 == strcmp(s, "dives")) {
		if (NULL == p->curlog)
			logwarnx(p, "<dives> not in <divelog>");
	} else if (0 == strcmp(s, "gasmixes")) {
		if (NULL == p->curdive)
			logwarnx(p, "<gasmix> not in <dive>");
		if (NULL != p->cursamp)
			logwarnx(p, "<gasmix> in <sample>");
	} else if (0 == strcmp(s, "samples")) {
		if (NULL == p->curdive)
			logwarnx(p, "<samples> not in <dive>");
		if (NULL != p->cursamp)
			logwarnx(p, "<samples> in <sample>");
	} else {
		if (NULL != p->cursamp)
			logwarnx(p, "%s: unknown <sample> child", s);
		else if (NULL != p->curdive)
			logwarnx(p, "%s: unknown <dive> child", s);
		else if (NULL != p->curlog)
			logwarnx(p, "%s: unknown <divelog> child", s);
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
divecmd_parse(const char *fname, XML_Parser p, 
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
divecmd_free(struct diveq *dq, struct divestat *st)
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
				free(s->events);
				free(s);
			}
			free(d->gas);
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
divecmd_init(XML_Parser *p, struct diveq *dq, 
	struct divestat *st, enum group group)
{

	if (NULL == (*p = XML_ParserCreate(NULL)))
		err(EXIT_FAILURE, NULL);

	TAILQ_INIT(dq);
	memset(st, 0, sizeof(struct divestat));
	st->group = group;
	TAILQ_INIT(&st->dlogs);
}

/*
 * If not NULL, prints the dive fingerprint.
 */
void
divecmd_print_dive_fingerprint(FILE *f, const struct dive *d)
{

	if (NULL == d->fprint)
		return;
	fprintf(f, "\t\t\t<fingerprint>%s</fingerprint>\n", d->fprint);
}

void
divecmd_print_dive_sampleq_close(FILE *f)
{

	fputs("\t\t\t</samples>\n", f);
}

void
divecmd_print_dive_sampleq_open(FILE *f)
{

	fputs("\t\t\t<samples>\n", f);
}

void
divecmd_print_dive_sampleq(FILE *f, const struct sampq *q)
{
	const struct samp *s;

	divecmd_print_dive_sampleq_open(f);
	TAILQ_FOREACH(s, q, entries)
		divecmd_print_dive_sample(f, s);
	divecmd_print_dive_sampleq_close(f);
}

/*
 * Print the dive sample.
 */
void
divecmd_print_dive_sample(FILE *f, const struct samp *s)
{
	size_t	 i, j;

	fprintf(f, "\t\t\t\t<sample time=\"%zu\">\n", s->time);

	if (SAMP_DEPTH & s->flags)
		fprintf(f, "\t\t\t\t\t"
			"<depth value=\"%g\" />\n", s->depth);
	if (SAMP_TEMP & s->flags)
		fprintf(f, "\t\t\t\t\t"
		        "<temp value=\"%g\" />\n", s->temp);
	if (SAMP_RBT & s->flags)
		fprintf(f, "\t\t\t\t\t"
		        "<rbt value=\"%zu\" />\n", s->rbt);
	for (j = 0; j < s->eventsz; j++)
		for (i = 0; i < EVENT__MAX; i++) {
			if ( ! ((1U << i) & s->events[j].bits))
				continue;
			fprintf(f, "\t\t\t\t\t"
			           "<event type=\"%s\""
			           " duration=\"%zu\" />\n", 
				   events[i],
				   s->events[j].duration);
			break;
		}
	fputs("\t\t\t\t</sample>\n", f);
}

void
divecmd_print_dive_close(FILE *f)
{

	fputs("\t\t</dive>\n", f);
}

void
divecmd_print_diveq_close(FILE *f)
{

	fputs("\t</dives>\n", f);
}

void
divecmd_print_diveq_open(FILE *f)
{

	fputs("\t<dives>\n", f);
}

void
divecmd_print_close(FILE *f)
{

	fputs("</divelog>\n", f);
}

/*
 * Print the <divelog> opening.
 * This must be followed by divecmd_print_dlog_close().
 */
void
divecmd_print_open(FILE *f, const struct dlog *dl)
{
	fprintf(f, 
		"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
		"<divelog program=\"divecmd2divecmd\" "
	         "version=\"" VERSION "\"");

	if (NULL != dl->ident)
		fprintf(f, " diver=\"%s\"", dl->ident);
	if (NULL != dl->product)
		fprintf(f, " product=\"%s\"", dl->product);
	if (NULL != dl->vendor)
		fprintf(f, " vendor=\"%s\"", dl->vendor);
	if (NULL != dl->model)
		fprintf(f, " model=\"%s\"", dl->model);

	fputs(">\n", f);
}

void
divecmd_print_dive(FILE *f, const struct dive *d)
{

	divecmd_print_dive_open(f, d);
	divecmd_print_dive_fingerprint(f, d);
	divecmd_print_dive_sampleq(f, &d->samps);
	divecmd_print_dive_close(f);
}

/*
 * Only prints number, date, time (datetime), mode.
 * Must be followed with divecmd_print_dive_close().
 */
void
divecmd_print_dive_open(FILE *f, const struct dive *d)
{
	struct tm	*tm;

	fputs("\t\t<dive", f);

	if (d->num)
		fprintf(f, " number=\"%zu\"", d->num);

	if (d->datetime) {
		tm = localtime(&d->datetime);
		fprintf(f, " date=\"%04d-%02d-%02d\""
		           " time=\"%02d:%02d:%02d\"",
			tm->tm_year + 1900, 
			tm->tm_mon + 1, tm->tm_mday, 
			tm->tm_hour, tm->tm_min, tm->tm_sec);
	}

	if (MODE_FREEDIVE == d->mode)
		fprintf(f, " mode=\"freedive\"");
	else if (MODE_GAUGE == d->mode)
		fprintf(f, " mode=\"gauge\"");
	else if (MODE_OC == d->mode)
		fprintf(f, " mode=\"opencircuit\"");
	else if (MODE_CC == d->mode)
		fprintf(f, " mode=\"closedcircuit\"");

	fputs(">\n", f);
}
