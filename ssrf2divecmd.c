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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libdivecomputer/descriptor.h>
#include <expat.h>

#include "parser.h"

int verbose = 0;

struct	parse {
	XML_Parser	  p; /* parser routine */
	const char	 *file; /* parsed filename */
	struct dlog	 *curlog; /* current divelog */
	struct dive	 *curdive; /* current dive */
	struct diveq	 *dives; /* all dives */
	struct divestat	 *stat; /* statistics */
	size_t		  pid; /* current dive no. */
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
lognattr(struct parse *p, const char *tag, const char *attr)
{

	logerrx(p, "missing <%s> attribute: %s", attr, tag);
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
 * Allocate a sample and insert it into the correct order.
 * FIXME we should just check the last one first and avoid walking the
 * whole queue, as most will be well-ordered.
 */
static struct samp *
samp_alloc(struct parse *p, size_t tm)
{
	struct samp	*samp, *ss;

	assert(NULL != p->curdive);

	samp = xcalloc(p, 1, sizeof(struct samp));
	samp->time = tm;

	TAILQ_FOREACH(ss, &p->curdive->samps, entries)
		if (ss->time >= samp->time)
			break;

	if (NULL == ss)
		TAILQ_INSERT_TAIL(&p->curdive->samps, samp, entries);
	else
		TAILQ_INSERT_BEFORE(ss, samp, entries);

	p->curdive->nsamps++;
	return samp;
}

static struct dgroup *
group_add(struct parse *p, size_t i, struct dive *d)
{
	struct dive	*dp = NULL;
	struct dgroup	*dg = p->stat->groups[i];

	d->group = dg;
	dg->ndives++;

	if (0 == d->datetime) {
		TAILQ_INSERT_TAIL(&dg->dives, d, gentries);
		return dg;
	}

	TAILQ_FOREACH(dp, &dg->dives, gentries)
		if (dp->datetime &&
		    dp->datetime > d->datetime)
			break;

	if (NULL == dp)
		TAILQ_INSERT_TAIL(&dg->dives, d, gentries);
	else
		TAILQ_INSERT_BEFORE(dp, d, gentries);

	return dg;
}

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
	return group_add(p, i, d);
}

/*
 * Parse depth in degrees celsius, "xx C".
 * Returns <0 on failure.
 */
static double
parse_temp(struct parse *p, const char *v)
{
	size_t	 sz;
	char	*cp;
	int	 rc;
	double	 val;

	if ((sz = strlen(v)) < 2)
		return 0;
	if (strcmp(" C", &v[sz - 2]))
		return 0;

	cp = xstrndup(p, v, sz - 2);
	rc = sscanf(cp, "%lg", &val);
	free(cp);

	return 1 != rc ? -1.0 : val;
}

/*
 * Parse pressure in bar, "xx bar".
 * Returns <0 on failure.
 */
static double
parse_pressure(struct parse *p, const char *v)
{
	size_t	 sz;
	char	*cp;
	int	 rc;
	double	 val;

	if ((sz = strlen(v)) < 4)
		return 0;
	if (strcmp(" bar", &v[sz - 4]))
		return 0;

	cp = xstrndup(p, v, sz - 4);
	rc = sscanf(cp, "%lg", &val);
	free(cp);

	return 1 != rc ? -1.0 : val;
}

/*
 * Parse depth in metres, "xx m".
 * Returns <0 on failure.
 */
static double
parse_depth(struct parse *p, const char *v)
{
	size_t	 sz;
	char	*cp;
	int	 rc;
	double	 val;

	if ((sz = strlen(v)) < 2)
		return 0;
	if (strcmp(" m", &v[sz - 2]))
		return 0;

	cp = xstrndup(p, v, sz - 2);
	rc = sscanf(cp, "%lg", &val);
	free(cp);

	return 1 != rc ? -1.0 : val;
}

/*
 * Parse a time formatted as "mm:ss min", with any number of digits in
 * the minute component.
 * Returns the time or 0 on failure.
 */
static int
parse_time(struct parse *p, const char *v, size_t *res)
{
	size_t	 sz;
	char	*cp;
	int	 rc, min, sec;

	*res = 0;

	if ((sz = strlen(v)) < 4)
		return 0;
	if (strcmp(" min", &v[sz - 4]))
		return 0;

	cp = xstrndup(p, v, sz - 4);
	rc = sscanf(cp, "%d:%d", &min, &sec);
	free(cp);

	if (2 != rc)
		return 0;

	*res = min * 60 + sec;
	return 1;
}

/*
 * Parse a date-time from the date and time component.
 * These must be in yyyy-mm-dd and hh:mm:ss formats.
 * Returns the epoch stamp or <0 on failure.
 */
static int
parse_percent(const char *val, double *res)
{
	size_t	 	 sz;
	long long	 v;
	char	 	 buf[32];
	const char	*er;

	if ((sz = strlen(val)) < 2 || sz > sizeof(buf) - 1) {
		warnx("1: %s", val);
		return 0;
	}
	if ('%' != val[sz - 1]) {
		warnx("2: %s", val);
		return 0;
	}

	strlcpy(buf, val, sizeof(buf));
	buf[sz - 1] = '\0';
	v = strtonum(buf, 0, 100, &er);
	if (NULL != er) {
		warnx("3: %s, %s", buf, er);
		return 0;
	}
	*res = v / 100.0;
	return 1;
}

/*
 * Parse a date-time from the date and time component.
 * These must be in yyyy-mm-dd and hh:mm:ss formats.
 * Returns the epoch stamp or <0 on failure.
 */
static time_t
parse_date(const char *date, const char *time)
{
	struct tm	 tm;
	int		 rc;

	memset(&tm, 0, sizeof(struct tm));
	rc = sscanf(date, "%d-%d-%d", 
		&tm.tm_year, &tm.tm_mon, &tm.tm_mday);
	if (3 != rc)
		return -1;

	tm.tm_year -= 1900;
	tm.tm_mon -= 1;

	rc = sscanf(time, "%d:%d:%d", 
		&tm.tm_hour, &tm.tm_min, &tm.tm_sec);
	if (3 != rc)
		return -1;

	tm.tm_isdst = -1;

	return mktime(&tm);
}

/*
 * Subsurface's format, very annoyingly, puts the vendor and product
 * into the same string.
 * So we need to break it down. 
 * We don't get the model number, unfortunately.
 */
static void
parse_divecomputerid(struct parse *p, const XML_Char **atts)
{
	const char	 *vendor, *product, *pmodel = NULL;
	char		 *buf;
	const XML_Char	**ap;
	dc_iterator_t	 *it;
	dc_descriptor_t  *d;

	if (NULL != p->curlog->vendor) {
		logerrx(p, "only one <divecomputerid> allowed");
		return;
	}

	for (ap = atts; NULL != ap[0]; ap += 2)
		if (0 == strcmp(ap[0], "model")) {
			pmodel = ap[1];
			break;
		}
	
	if (NULL == pmodel) {
		lognattr(p, "divecomputerid", "model");
		return;
	}

	if (DC_STATUS_SUCCESS != dc_descriptor_iterator(&it)) {
		logerrx(p, "dc_descriptor_iterator");
		return;
	}

	while (DC_STATUS_SUCCESS == dc_iterator_next(it, &d)) {
		vendor = dc_descriptor_get_vendor(d);
		product = dc_descriptor_get_product(d);
		asprintf(&buf, "%s %s", vendor, product);
		if (0 == strcmp(buf, pmodel)) {
			p->curlog->vendor = xstrdup(p, vendor);
			p->curlog->product = xstrdup(p, product);
			free(buf);
			dc_descriptor_free(d);
			break;
		}
		free(buf);
		dc_descriptor_free(d);
	}

	dc_iterator_free(it);
}

static void 
parse_event(struct parse *p, const XML_Char **atts)
{
	struct samp	 *samp = NULL;
	int		  type = 0;
	size_t		  tm;
	const XML_Char	**ap;
	const char	 *typep = NULL, *tmp = NULL, *flagp = NULL;
	
	/* All events must have a time and type. */

	for (ap = atts; NULL != ap[0]; ap += 2) 
		if (0 == strcmp(*ap, "type")) 
			typep = ap[1];
		else if (0 == strcmp(*ap, "time"))
			tmp = ap[1];
		else if (0 == strcmp(*ap, "flags"))
			flagp = ap[1];

	if (NULL == typep) {
		lognattr(p, "event", "type");
		return;
	} else if (NULL == tmp) {
		lognattr(p, "event", "time");
		return;
	}

	type = atoi(typep);
	if (type < 0 || type > EVENT_gaschange2) {
		logerrx(p, "bad <event> type");
		return;
	} else if ( ! parse_time(p, tmp, &tm)) {
		logerrx(p, "bad <event> time");
		return;
	}

	/* Each is going to be a singleton. */

	samp = samp_alloc(p, tm);
	samp->events = xcalloc(p, 1, sizeof(struct sampevent));
	samp->eventsz++;
	samp->events[0].type = type;
	if (NULL != flagp) 
		samp->events[0].flags = atoi(flagp);
}

static void 
parse_sample(struct parse *p, const XML_Char **atts)
{
	const XML_Char	**ap;
	struct samp	 *samp;
	struct dive	 *d = p->curdive;
	const char	 *v;
	size_t		  i, sz, tm, tank;

	for (v = NULL, ap = atts; NULL != ap[0]; ap += 2)
		if (0 == strcmp(*ap, "time"))
			v = ap[1];

	if (NULL == v) {
		lognattr(p, "sample", "time");
		return;
	}

	if ( ! parse_time(p, v, &tm)) {
		logerrx(p, "bad <sample> time");
		return;
	} 

	logdbg(p, "new sample at %zu", tm);
	samp = samp_alloc(p, tm);

	if (samp->time > d->maxtime)
		d->maxtime = samp->time;
	if (d->datetime &&
	    d->datetime + (time_t)samp->time > 
	    p->stat->timestamp_max)
		p->stat->timestamp_max =
			d->datetime + (time_t)samp->time;

	for (ap = atts; NULL != ap[0]; ap += 2)
		if (0 == strcmp(*ap, "depth")) {
			samp->depth = parse_depth(p, ap[1]);
			if (samp->depth < 0.0) {
				logerrx(p, "bad <sample> depth");
				return;
			}
			samp->flags |= SAMP_DEPTH;
		} else if (0 == strcmp(*ap, "rbt")) {
			if ( ! parse_time(p, ap[1], &samp->rbt)) {
				logerrx(p, "bad <sample> depth");
				return;
			}
			samp->flags |= SAMP_RBT;
		} else if (0 == strcmp(*ap, "temp")) {
			samp->temp = parse_temp(p, ap[1]);
			if (samp->temp < 0.0) {
				logerrx(p, "bad <sample> temp");
				return;
			}
			samp->flags |= SAMP_TEMP;
		} else if (0 == strncmp(*ap, "pressure", 8)) {
			samp->pressure = xreallocarray
				(p, samp->pressure,
				 samp->pressuresz + 1,
				 sizeof(struct samppres));
			samp->pressuresz++;
			samp->pressure[samp->pressuresz - 1].pressure = 
				parse_pressure(p, ap[1]);
			if (samp->pressure[samp->pressuresz - 1].pressure < 0.0) {
				logerrx(p, "bad <sample> pressure");
				return;
			}
			sz = strlen(*ap);
			tank = sz > 8 ? atoi(*ap + 8) : 0;
			samp->pressure[samp->pressuresz - 1].tank = tank + 1;
			for (i = 0; i < d->cylsz; i++)
				if (tank + 1 == d->cyls[i].num)
					break;
			if (i == d->cylsz) {
				logerrx(p, "<sample> tank not found");
				return;
			}
		} else if (0 == strcmp(*ap, "cns")) {
			if ( ! parse_percent(ap[1], &samp->cns)) {
				logerrx(p, "bad <sample> cns");
				return;
			}
			samp->flags |= SAMP_CNS;
		} else if (0 == strcmp(*ap, "ndl")) {
			memset(&samp->deco, 0, sizeof(struct sampdeco));
			if ( ! parse_time(p, ap[1], &samp->deco.duration)) {
				logerrx(p, "bad <sample> ndl");
				return;
			}
			samp->deco.type = DECO_ndl;
			samp->flags |= SAMP_DECO;
		} else if (strcmp(*ap, "time"))
			logwarnx(p, "unknown <sample> attribute: %s", ap[0]);

	if (SAMP_DEPTH & samp->flags)
		if (samp->depth > p->curdive->maxdepth)
			p->curdive->maxdepth = samp->depth;

	if (SAMP_TEMP & samp->flags) {
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
	}
}

static void
parse_cylinder(struct parse *p, const XML_Char **atts)
{
	const XML_Char	**ap;
	struct dive	 *d = p->curdive;
	char		 *ep, *mixes[3];

	mixes[0] = mixes[1] = mixes[2] = NULL;
	for (ap = atts; NULL != ap[0]; ap += 2)
		if (0 == strcmp(*ap, "o2")) {
			free(mixes[0]);
			mixes[0] = xstrdup(p, ap[1]);
		} else if (0 == strcmp(*ap, "n2")) {
			free(mixes[1]);
			mixes[1] = xstrdup(p, ap[1]);
		} else if (0 == strcmp(*ap, "he")) {
			free(mixes[2]);
			mixes[2] = xstrdup(p, ap[1]);
		} else if (0 == strcmp(*ap, "description")) {
			/* Do nothing. */ ;
		} else
			logattr(p, "cylinder", *ap);

	d->gas = xreallocarray(p, d->gas, 
		d->gassz + 1, sizeof(struct divegas));
	d->gas[d->gassz].num = d->gassz + 1;

	if (NULL != mixes[0]) {
		if ('\0' != mixes[0][0] &&
		    '%' == mixes[0][strlen(mixes[0]) - 1])
			mixes[0][strlen(mixes[0]) - 1] = '\0';
		d->gas[d->gassz].o2 = strtod(mixes[0], &ep);
		if (ep == mixes[0] || ERANGE == errno) {
			logerrx(p, "bad <o2> value");
			return;
		}
	}

	if (NULL != mixes[1]) {
		if ('\0' != mixes[1][0] &&
		     '%' == mixes[1][strlen(mixes[1]) - 1])
			mixes[1][strlen(mixes[1]) - 1] = '\0';
		d->gas[d->gassz].n2 = strtod(mixes[1], &ep);
		if (ep == mixes[1] || ERANGE == errno) {
			logerrx(p, "bad <n2> value");
			return;
		}
	}

	if (NULL != mixes[2]) {
		if ('\0' != mixes[2][0] &&
		    '%' == mixes[2][strlen(mixes[2]) - 1])
			mixes[2][strlen(mixes[2]) - 1] = '\0';
		d->gas[d->gassz].he = strtod(mixes[2], &ep);
		if (ep == mixes[2] || ERANGE == errno) {
			logerrx(p, "bad <he> value");
			return;
		}
	}
	free(mixes[0]);
	free(mixes[1]);
	free(mixes[2]);

	d->cyls = xreallocarray(p, d->cyls,
		d->cylsz + 1, sizeof(struct cylinder));
	d->cyls[d->cylsz].mix = d->gas[d->gassz].num;
	d->cyls[d->cylsz].num = d->cylsz + 1;
	d->cylsz++;
	d->gassz++;
}

static void
parse_open(void *dat, const XML_Char *s, const XML_Char **atts)
{
	const XML_Char	**ap;
	struct parse	 *p = dat;
	struct dive	 *d, *dp;
	const char	 *date, *time, *num, *er, *dur, *mode, *v;
	struct dgroup	 *grp;

	if (0 == strcmp(s, "divelog")) {
		if (NULL != p->curlog) {
			logerrx(p, "nested <divelog>");
			return;
		}
		p->curlog = xcalloc(p, 1, sizeof(struct dlog));
		p->curlog->file = xstrdup(p, p->file);
		p->curlog->line = XML_GetCurrentLineNumber(p->p);
		TAILQ_INSERT_TAIL(&p->stat->dlogs, p->curlog, entries);
		logdbg(p, "new divelog");
	} else if (0 == strcmp(s, "divecomputer")) {
		if (NULL == p->curdive) {
			logerrx(p, "<divecomputer> not in <dive>");
			return;
		}
		v = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "diveid"))
				v = ap[1];
		if (NULL != v)
			p->curdive->fprint = xstrdup(p, v);
	} else if (0 == strcmp(s, "dive")) {
		if (NULL != p->curdive) {
			logerrx(p, "nested <dive>");
			return;
		} else if (NULL == p->curlog) {
			logerrx(p, "<dive> not in <divelog>");
			return;
		}

		p->curdive = d = xcalloc(p, 1, sizeof(struct dive));
		p->curdive->pid = ++p->pid;
		p->curdive->line = XML_GetCurrentLineNumber(p->p);
		TAILQ_INIT(&d->samps);
		d->log = p->curlog;
		d->mode = MODE_OC;

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
				logwarnx(p, "%s: bad "
					"<dive> mode", mode);
		}

		if (NULL != num) {
			d->num = strtonum(num, 0, LONG_MAX, &er);
			if (NULL != er) {
				logerrx(p, "bad <dive> number");
				return;
			} else
				logdbg(p, "new dive: %zu", d->num);
		}

		if (NULL != dur && 
		    ! parse_time(p, dur, &d->duration)) {
			logerrx(p, "bad <dive> duration");
			return;
		}

		if (NULL != date && NULL != time) {
			d->datetime = parse_date(date, time);
			if (d->datetime < 0) {
				logerrx(p, "bad <dive> date/time");
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

		if (0 == p->stat->groupsz && verbose)
			logdbg(p, "new default group");
		grp = (0 == p->stat->groupsz) ?
			group_alloc(p, d, NULL) :
			group_add(p, 0, d);

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
	} else if (0 == strcmp(s, "cylinder")) {
		if (NULL == p->curdive) { 
			logerrx(p, "<cylinder> not in <dive>");
			return;
		}
		parse_cylinder(p, atts);
	} else if (0 == strcmp(s, "sample")) {
		if (NULL == p->curdive) { 
			logerrx(p, "<sample> not in <dive>");
			return;
		}
		parse_sample(p, atts);
	} else if (0 == strcmp(s, "event")) {
		if (NULL == p->curdive) { 
			logerrx(p, "<event> not in <dive>");
			return;
		}
		parse_event(p, atts);
	} else if (0 == strcmp(s, "extradata")) {
		logwarnx(p, "ignoring <extradata>");
	} else if (0 == strcmp(s, "dives")) {
		if (NULL == p->curlog)
			logerrx(p, "<dives> not in <divelog>");
	} else if (0 == strcmp(s, "settings")) {
		if (NULL != p->curdive)
			logerrx(p, "<settings> in <dive>");
	} else if (0 == strcmp(s, "divecomputerid")) {
		if (NULL == p->curlog) {
			logerrx(p, "<divecomputerid> not in <divelog>");
			return;
		}
		parse_divecomputerid(p, atts);
	} else {
		if (NULL != p->curdive)
			logwarnx(p, "%s: unknown <dive> child", s);
		else if (NULL != p->curlog)
			logwarnx(p, "%s: unknown <divelog> child", s);
		else
			logwarnx(p, "%s: unknown element", s);
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
}

static int
ssrf_parse(const char *fname, XML_Parser p, 
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
		return 0;
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
	return 0 == ssz;
}

int
main(int argc, char *argv[])
{
	struct diveq	 dq;
	struct dive	*d;
	struct divestat	 st;
	XML_Parser	 p;
	const char	*ident = NULL;
	int		 c, rc = 0;
	size_t		 i;

#if HAVE_PLEDGE
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (c = getopt(argc, argv, "i:v")))
		switch (c) {
		case ('i'):
			ident = optarg;
			break;
		case ('v'):
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
		if ( ! ssrf_parse("-", p, &dq, &st))
			goto out;

	for (i = 0; i < (size_t)argc; i++)
		if ( ! ssrf_parse(argv[i], p, &dq, &st))
			goto out;

#if HAVE_PLEDGE
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	if (TAILQ_EMPTY(&st.dlogs)) {
		warnx("no divelogs");
		goto out;
	} 
	
	if (NULL != TAILQ_NEXT(TAILQ_FIRST(&st.dlogs), entries)) {
		warnx("too many divelogs");
		goto out;
	}

	TAILQ_FIRST(&st.dlogs)->ident = 
		NULL == ident ? NULL : strdup(ident);
	if (NULL != ident && NULL == TAILQ_FIRST(&st.dlogs)->ident)
		err(EXIT_FAILURE, NULL);

	divecmd_print_open(stdout, TAILQ_FIRST(&st.dlogs));
	divecmd_print_diveq_open(stdout);
	TAILQ_FOREACH(d, &dq, entries)
		divecmd_print_dive(stdout, d);
	divecmd_print_diveq_close(stdout);
	divecmd_print_close(stdout);
	rc = 1;
out:
	divecmd_free(&dq, &st);
	return rc ? EXIT_SUCCESS : EXIT_FAILURE;
usage:
	fprintf(stderr, "usage: %s [-v] [file ...]\n", getprogname());
	return EXIT_FAILURE;
}

