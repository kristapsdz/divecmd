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
#include <float.h>
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
	size_t		  pid;
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

	logwarnx(p, "unknown <%s> attribute: %s", tag, attr);
}

static void
lognattr(struct parse *p, const char *tag, const char *attr)
{

	logerrx(p, "missing <%s> attribute: %s", tag, attr);
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

/*
 * Convert "val" to a double leaving it as 0.0 upon conversion errors.
 * Return zero on failure, non-zero on success (the pointer will be set
 * to zero).
 */
static int
xstrtod(const char *val, double *res)
{
	char		 *ep;

	*res = strtod(val, &ep);
	if (ep == val || ERANGE == errno) {
		*res = 0.0;
		return 0;
	}

	if (*res <= FLT_EPSILON)
		*res = 0.0;

	return 1;
}

static void
logerrp(struct parse *p)
{

	logerrx(p, "%s", XML_ErrorString(XML_GetErrorCode(p->p)));
}

/*
 * Some group sortings need to be sorted /after/ the whole dive has been
 * processed---for example, when sorting by maximum depth that can only
 * be ascertained after all samples have been read.
 * This should be called when closing out a dive.
 */
static void
group_readd(struct parse *p, struct dive *d)
{
	struct dive	*dp;

	assert(NULL != d->group);

	/* Date-time is a pre-processing thing, so nothing to do. */

	if (GROUPSORT_DATETIME == p->stat->groupsort)
		return;

	/* Start by removing, then we'll re-add. */

	TAILQ_REMOVE(&d->group->dives, d, gentries);

	if (GROUPSORT_MAXTIME == p->stat->groupsort) {
		TAILQ_FOREACH(dp, &d->group->dives, gentries)
			if (dp->maxtime &&
			    dp->maxtime > d->maxtime)
				break;
	} else if (GROUPSORT_RMAXTIME == p->stat->groupsort) {
		TAILQ_FOREACH(dp, &d->group->dives, gentries)
			if (dp->maxtime &&
			    dp->maxtime < d->maxtime)
				break;
	} else if (GROUPSORT_RMAXDEPTH == p->stat->groupsort) {
		TAILQ_FOREACH(dp, &d->group->dives, gentries)
			if (dp->maxdepth &&
			    dp->maxdepth < d->maxdepth)
				break;
	} else {
		TAILQ_FOREACH(dp, &d->group->dives, gentries)
			if (dp->maxdepth &&
			    dp->maxdepth > d->maxdepth)
				break;
	}

	if (NULL == dp)
		TAILQ_INSERT_TAIL(&d->group->dives, d, gentries);
	else
		TAILQ_INSERT_BEFORE(dp, d, gentries);
}

/*
 * Add the dive "d" to the group "i" (which must exist) indexing in the
 * array of groups.
 * Returns the group (never NULL).
 * This inserts the groups in sorted error, if applicable.
 * Be sure to group_readd after closing out the dive, because some
 * sorting criteria are post-processed (e.g., maximum depth).
 */
static struct dgroup *
group_add(struct parse *p, size_t i, struct dive *d)
{
	struct dive	*dp = NULL;
	struct dgroup	*dg = p->stat->groups[i];

	d->group = dg;
	dg->ndives++;

	/* All other sorting types need group_readd(). */

	if (GROUPSORT_DATETIME == p->stat->groupsort) {
		if (0 == d->datetime) {
			TAILQ_INSERT_TAIL(&dg->dives, d, gentries);
			return(dg);
		}
		TAILQ_FOREACH(dp, &dg->dives, gentries)
			if (dp->datetime &&
			    dp->datetime > d->datetime)
				break;
	}

	if (NULL == dp)
		TAILQ_INSERT_TAIL(&dg->dives, d, gentries);
	else
		TAILQ_INSERT_BEFORE(dp, d, gentries);

	return(dg);
}

/*
 * Add a new group, optionally with date "date" (NULL is ok), to the set
 * of groups.
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
group_lookup_name(struct parse *p, struct dive *d, const char *name)
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

/*
 * Look for a group registered with the same divelog.
 * It must be identical: diver, vendor, product, etc.
 * Return a pointer to the group or NULL.
 */
static struct dgroup *
group_lookup_divelog(struct parse *p, struct dive *d)
{
	struct dgroup 		*dg;
	const struct dlog	*dl;
	size_t			 i;

	for (i = 0; i < p->stat->groupsz; i++) {
		dg = p->stat->groups[i];
		assert(dg->ndives);
		assert(NULL != TAILQ_FIRST(&dg->dives));
		dl = TAILQ_FIRST(&dg->dives)->log;

		if (NULL == dl->vendor && NULL != d->log->vendor)
			continue;
		if (NULL != dl->vendor && NULL == d->log->vendor)
			continue;
		if (NULL != dl->vendor &&
	 	    strcmp(dl->vendor, d->log->vendor))
			continue;
		if (NULL == dl->model && NULL != d->log->model)
			continue;
		if (NULL != dl->model && NULL == d->log->model)
			continue;
		if (NULL != dl->model &&
	 	    strcmp(dl->model, d->log->model))
			continue;
		if (NULL == dl->product && NULL != d->log->product)
			continue;
		if (NULL != dl->product && NULL == d->log->product)
			continue;
		if (NULL != dl->product &&
	 	    strcmp(dl->product, d->log->product))
			continue;
		if (NULL == dl->ident && NULL != d->log->ident)
			continue;
		if (NULL != dl->ident && NULL == d->log->ident)
			continue;
		if (NULL != dl->ident &&
	 	    strcmp(dl->ident, d->log->ident))
			continue;
		return(group_add(p, i, d));
	}

	if (verbose)
		fprintf(stderr, "%s: new group: %s, %s, %s, %s\n", 
			p->file, 
			NULL == d->log->ident ?
			"(no diver ident)" : d->log->ident,
			NULL == d->log->vendor ?
			"(no vendor)" : d->log->vendor,
			NULL == d->log->product ?
			"(no product)" : d->log->product,
			NULL == d->log->model ?
			"(no model)" : d->log->model);

	return(group_alloc(p, d, NULL));
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
parse_tank(struct parse *p, const XML_Char **atts)
{
	struct dive	 *d = p->curdive;
	const char	 *tank = NULL, *mix = NULL, *er,
	      	 	 *vol = NULL, *wp = NULL;
	const XML_Char	**ap;
	size_t		  i;

	for (ap = atts; NULL != ap[0]; ap += 2)
		if (0 == strcmp(*ap, "num"))
			tank = ap[1];
		else if (0 == strcmp(*ap, "gasmix"))
			mix = ap[1];
		else if (0 == strcmp(*ap, "volume"))
			vol = ap[1];
		else if (0 == strcmp(*ap, "workpressure"))
			wp = ap[1];
		else
			logattr(p, "tank", *ap);

	if (NULL == tank) {
		lognattr(p, "tank", "num");
		return;
	}

	i = strtonum(tank, 0, LONG_MAX, &er);
	if (NULL != er) {
		logerrx(p, "malformed <tank> num: %s", tank);
		return;
	}

	d->cyls = xreallocarray(p, d->cyls, 
		d->cylsz + 1, sizeof(struct cylinder));
	memset(&d->cyls[d->cylsz], 0, sizeof(struct cylinder));
	d->cyls[d->cylsz].num = i;

	if (NULL != mix) {
		i = strtonum(mix, 0, LONG_MAX, &er);
		if (NULL != er) {
			logerrx(p, "malformed <tank> mix: %s", mix);
			return;
		}
		d->cyls[d->cylsz].mix = i;
	}

	if (NULL != vol && 
	    ! xstrtod(vol, &d->cyls[d->cylsz].size))
		logwarnx(p, "malformed <tank> size: %s", vol);
	if (NULL != wp && 
	    ! xstrtod(wp, &d->cyls[d->cylsz].workpressure))
		logwarnx(p, "malformed <tank> workpressure: %s", wp);

	d->cylsz++;
}

static void
parse_gasmix(struct parse *p, const XML_Char **atts)
{
	struct dive	 *d = p->curdive;
	const char	 *mixes[3];
	const char	 *v, *er;
	const XML_Char	**ap;
	size_t		  i;

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
		lognattr(p, "gasmix", "num");
		return;
	}

	i = strtonum(v, 0, LONG_MAX, &er);
	if (NULL != er) {
		logerrx(p, "malformed <gasmix> num: %s", v);
		return;
	}

	d->gas = xreallocarray(p, d->gas, 
		d->gassz + 1, sizeof(struct divegas));
	memset(&d->gas[d->gassz], 0, sizeof(struct divegas));
	d->gas[d->gassz].num = i;

	if (NULL != mixes[0] &&
	    ! xstrtod(mixes[0], &d->gas[d->gassz].o2))
		logwarnx(p, "malformed <gasmix> o2: %s", mixes[0]);
	else if (NULL == mixes[0])
		logwarnx(p, "missing <gasmix> o2");

	if (NULL != mixes[1] &&
	    ! xstrtod(mixes[1], &d->gas[d->gassz].n2))
		logwarnx(p, "malformed <gasmix> n2: %s", mixes[1]);

	if (NULL != mixes[2] &&
	    ! xstrtod(mixes[2], &d->gas[d->gassz].he))
		logwarnx(p, "malformed <gasmix> he: %s", mixes[2]);

	d->gassz++;
}

static void
parse_pressure(struct parse *p, const XML_Char **atts)
{
	const char	 *num = NULL, *v = NULL, *er;
	const XML_Char	**ap;
	struct samp	 *s = p->cursamp;

	for (ap = atts; NULL != ap[0]; ap += 2)
		if (0 == strcmp(*ap, "value"))
			v = ap[1];
		else if (0 == strcmp(*ap, "tank"))
			num = ap[1];
		else
			logattr(p, "pressure", *ap);

	if (NULL == v) {
		lognattr(p, "pressure", "value");
		return;
	} else if (NULL == num) {
		lognattr(p, "pressure", "tank");
		return;
	}

	s->pressure = xreallocarray
		(p, s->pressure,
		 s->pressuresz + 1,
		 sizeof(struct samppres));
	s->pressuresz++;

	if ( ! xstrtod(v, &s->pressure[s->pressuresz - 1].pressure)) {
		logerrx(p, "malformed <pressure> value: %s", v);
		return;
	}

	s->pressure[s->pressuresz - 1].tank = strtonum
		(num, 0, LONG_MAX, &er);
	if (NULL != er) {
		logerrx(p, "bad <pressure> tank");
		return;
	}
}

static void
parse_event(struct parse *p, const XML_Char **atts)
{
	const XML_Char	**ap;
	struct samp	 *s = p->cursamp;
	const char	 *v = NULL, *dur = NULL, *fl = NULL, *er;
	enum event	  evt;

	for (ap = atts; NULL != ap[0]; ap += 2)
		if (0 == strcmp(*ap, "type"))
			v = ap[1];
		else if (0 == strcmp(*ap, "duration"))
			dur = ap[1];
		else if (0 == strcmp(*ap, "flags"))
			fl = ap[1];
		else
			logattr(p, "event", *ap);

	if (NULL == v) {
		lognattr(p, "event", "type");
		return;
	}

	for (evt = 0; evt < EVENT__MAX; evt++)
		if (0 == strcmp(v, events[evt]))
			break;

	if (EVENT__MAX == evt) {
		logerrx(p, "unknown <event> type");
		return;
	}

	s->events = xreallocarray
		(p, s->events, 
		 s->eventsz + 1,
		 sizeof(struct sampevent));
	memset(&s->events[s->eventsz], 0,
		sizeof(struct sampevent));
	s->events[s->eventsz].type = evt;
	s->eventsz++;

	if (NULL != dur) {
		s->events[s->eventsz - 1].duration = 
			strtonum(dur, 0, LONG_MAX, &er);
		if (NULL != er)
			logerrx(p, "bad <event> duration: %s", er);
	}
	if (NULL != fl) {
		s->events[s->eventsz - 1].flags = 
			strtonum(fl, 0, LONG_MAX, &er);
		if (NULL != er)
			logerrx(p, "bad <event> flags: %s", er);
	}
}

static void
parse_deco(struct parse *p, const XML_Char **atts)
{
	const XML_Char	**ap;
	struct samp	*samp = p->cursamp;
	const char	*depth = NULL, *mode = NULL, 
	     		*dur = NULL, *er;

	for (ap = atts; NULL != ap[0]; ap += 2) 
		if (0 == strcmp(*ap, "depth"))
			depth = ap[1];
		else if (0 == strcmp(*ap, "type"))
			mode = ap[1];
		else if (0 == strcmp(*ap, "duration"))
			dur = ap[1];
		else
			logattr(p, "deco", *ap);

	if (NULL == mode) {
		lognattr(p, "deco", "type");
		return;
	}

	for (samp->deco.type = 0; 
	     samp->deco.type < DECO__MAX; 
	     samp->deco.type++)
		if (0 == strcmp(decos[samp->deco.type], mode))
			break;

	if (DECO__MAX == samp->deco.type) {
		logerrx(p, "unknown <deco> type");
		return;
	}

	if (NULL != depth && ! xstrtod(depth, &samp->deco.depth)) {
		logerrx(p, "malformed <deco> depth");
		return;
	}

	if (NULL != dur) {
		samp->deco.duration = 
			strtonum(dur, 0, LONG_MAX, &er);
		if (NULL != er) {
			logerrx(p, "malformed <deco> duration: %s", er);
			return;
		}
	}

	samp->flags |= SAMP_DECO;
}

static void
parse_open(void *dat, const XML_Char *s, const XML_Char **atts)
{
	const XML_Char	**ap;
	struct parse	 *p = dat;
	struct samp	 *samp;
	struct dive	 *d, *dp;
	const char	 *date, *time, *num, *er, *dur, *mode, *v;
	struct tm	  tm;
	int		  rc;
	struct dgroup	 *grp;
	size_t		  i;

	if (0 == strcmp(s, "divelog")) {
		if (NULL != p->curlog) {
			logerrx(p, "nested <divelog>");
			return;
		}
		p->curlog = xcalloc(p, 1, sizeof(struct dlog));
		p->curlog->file = xstrdup(p, p->file);
		p->curlog->line = XML_GetCurrentLineNumber(p->p);
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
		p->curdive->pid = ++p->pid;
		p->curdive->line = XML_GetCurrentLineNumber(p->p);
		p->curdive->col = XML_GetCurrentColumnNumber(p->p);
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
				grp = group_lookup_name(p, d, "");
			} else
				grp = group_lookup_name(p, d, date);
		} else if (GROUP_DIVER == p->stat->group) {
			if (NULL == d->log->ident) {
				logwarnx(p, "group "
					"<dive> without diver");
				grp = group_lookup_name(p, d, "");
			} else
				grp = group_lookup_name(p, 
					d, d->log->ident);
		} else if (GROUP_DIVELOG == p->stat->group) {
			assert(NULL != d->log);
			grp = group_lookup_divelog(p, d);
			assert(NULL != grp);
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
		if (NULL == (d = p->curdive))
			logerrx(p, "<fingerprint> not in <dive>");
		else if (NULL != d->fprint)
			logerrx(p, "restatement of <fingerprint>");
		else if (p->bufsz)
			logerrx(p, "nested <fingerprint>");
		else
			XML_SetDefaultHandler(p->p, parse_text);
	} else if (0 == strcmp(s, "gasmix")) {
		if (NULL == p->curdive) { 
			logerrx(p, "<gasmix> not in <dive>");
			return;
		}
		parse_gasmix(p, atts);
	} else if (0 == strcmp(s, "tank")) {
		if (NULL == p->curdive) { 
			logerrx(p, "<tank> not in <dive>");
			return;
		}
		parse_tank(p, atts);
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
			lognattr(p, "sample", "time");
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
		samp->line = XML_GetCurrentLineNumber(p->p);
		samp->col = XML_GetCurrentColumnNumber(p->p);

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
	} else if (0 == strcmp(s, "vendor")) {
		if (NULL == (samp = p->cursamp)) {
			logerrx(p, "<vendor> not in <sample>");
			return;
		} else if (SAMP_VENDOR & samp->flags) {
			logerrx(p, "restatement of <vendor>");
			return;
		}
		v = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "type"))
				v = ap[1];
			else
				logattr(p, "vendor", *ap);
		if (NULL == v) {
			lognattr(p, "vendor", "type");
			return;
		}
		samp->vendor.type = strtonum(v, 0, LONG_MAX, &er);
		if (NULL != er) {
			logerrx(p, "malformed <vendor> type: %s", v);
			return;
		}
		XML_SetDefaultHandler(p->p, parse_text);
		samp->flags |= SAMP_VENDOR;
	} else if (0 == strcmp(s, "depth")) {
		if (NULL == (samp = p->cursamp))
			return;
		if (SAMP_DEPTH & samp->flags) {
			logerrx(p, "restatement of <depth>");
			return;
		}

		v = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "value"))
				v = ap[1];
			else
				logattr(p, "depth", *ap);
		if (NULL == v) {
			lognattr(p, "depth", "value");
			return;
		}

		if ( ! xstrtod(v, &samp->depth)) {
			logerrx(p, "malformed <depth> value: %s", v);
			return;
		}

		if (samp->depth > p->curdive->maxdepth)
			p->curdive->maxdepth = samp->depth;
		samp->flags |= SAMP_DEPTH;
	} else if (0 == strcmp(s, "pressure")) {
		if (NULL == p->cursamp)
			return;
		parse_pressure(p, atts);
	} else if (0 == strcmp(s, "rbt")) {
		if (NULL == (samp = p->cursamp)) {
			logerrx(p, "<rbt> not in <sample>");
			return;
		} else if (SAMP_RBT & samp->flags) {
			logerrx(p, "restatement of <rbt>");
			return;
		}

		v = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "value"))
				v = ap[1];
			else
				logattr(p, "rbt", *ap);
		if (NULL == v) {
			lognattr(p, "rbt", "value");
			return;
		}

		samp->rbt = strtonum(v, 0, LONG_MAX, &er);
		if (NULL != er) {
			logerrx(p, "malformed <rbt> value: %s", v);
			return;
		}
		samp->flags |= SAMP_RBT;
	} else if (0 == strcmp(s, "event")) {
		if (NULL == p->cursamp) {
			logerrx(p, "<event> not in <sample>");
			return;
		} else
			parse_event(p, atts);
	} else if (0 == strcmp(s, "deco")) {
		/* Ignore deco when freediving. */
		if (NULL == (samp = p->cursamp))
			logerrx(p, "<deco> not in <sample>");
		else if (SAMP_DECO & samp->flags)
			logerrx(p, "restatement of <deco>");
		else if (MODE_FREEDIVE != p->curdive->mode)
			parse_deco(p, atts);
	} else if (0 == strcmp(s, "temp")) {
		if (NULL == (samp = p->cursamp)) {
			logerrx(p, "<temp> not in <sample>");
			return;
		} else if (SAMP_TEMP & samp->flags) {
			logerrx(p, "restatement of <temp>");
			return;
		}

		v = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "value"))
				v = ap[1];
			else
				logattr(p, "temp", *ap);
		if (NULL == v) {
			lognattr(p, "temp", "value");
			return;
		}

		if ( ! xstrtod(v, &samp->temp)) {
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
	} else if (0 == strcmp(s, "cns")) {
		if (NULL == (samp = p->cursamp)) {
			logerrx(p, "<cns> not in <sample>");
			return;
		} else if (SAMP_CNS & samp->flags) {
			logerrx(p, "restatement of <cns>");
			return;
		}

		for (v = NULL, ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "value"))
				v = ap[1];
			else
				logattr(p, "cns", *ap);

		if (NULL == v) {
			lognattr(p, "cns", "value");
			return;
		} else if ( ! xstrtod(v, &samp->cns)) {
			logerrx(p, "malformed <cns> value: %s", v);
			return;
		}
		samp->flags |= SAMP_CNS;
	} else if (0 == strcmp(s, "gaschange")) {
		if (NULL == (samp = p->cursamp)) {
			logerrx(p, "<gaschange> not in <sample>");
			return;
		} else if (SAMP_GASCHANGE & samp->flags) {
			logerrx(p, "restatement of <gaschange>");
			return;
		}

		v = NULL;
		for (ap = atts; NULL != ap[0]; ap += 2)
			if (0 == strcmp(*ap, "mix"))
				v = ap[1];
			else
				logattr(p, "gaschange", *ap);
		if (NULL == v) {
			lognattr(p, "gaschange", "mix");
			return;
		}

		samp->gaschange = strtonum(v, 0, UINT_MAX, &er) + 1;
		if (NULL != er) {
			logerrx(p, "bad <gaschange> mix: %s", er);
			return;
		}
		for (i = 0; i < p->curdive->gassz; i++)
			if (samp->gaschange == p->curdive->gas[i].num)
				break;
		if (i == p->curdive->gassz) {
			logerrx(p, "unknown <gaschange> mix: %s", v);
			return;
		}
		samp->flags |= SAMP_GASCHANGE;
	} else if (0 == strcmp(s, "dives")) {
		if (NULL == p->curlog)
			logerrx(p, "<dives> not in <divelog>");
	} else if (0 == strcmp(s, "gasmixes")) {
		if (NULL == p->curdive)
			logerrx(p, "<gasmixes> not in <dive>");
		else if (NULL != p->cursamp)
			logerrx(p, "<gasmixes> in <sample>");
		else if (p->curdive->gassz) 
			logerrx(p, "restatement of <gasmixes>");
	} else if (0 == strcmp(s, "tanks")) {
		if (NULL == p->curdive)
			logerrx(p, "<tanks> not in <dive>");
		else if (NULL != p->cursamp)
			logerrx(p, "<tanks> in <sample>");
		else if (p->curdive->cylsz) 
			logerrx(p, "restatement of <tanks>");
	} else if (0 == strcmp(s, "samples")) {
		if (NULL == p->curdive)
			logerrx(p, "<samples> not in <dive>");
		else if (NULL != p->cursamp)
			logerrx(p, "<samples> in <sample>");
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
		group_readd(p, p->curdive);
		p->curdive = NULL;
	} else if (0 == strcmp(s, "sample")) {
		p->cursamp = NULL;
	} else if (0 == strcmp(s, "vendor")) {
		XML_SetDefaultHandler(p->p, NULL);
		if (NULL != p->cursamp) {
			free(p->cursamp->vendor.buf);
			p->cursamp->vendor.buf = 
				xstrndup(p, p->buf, p->bufsz);
		}
		free(p->buf);
		p->buf = NULL;
		p->bufsz = 0;
	}

}

/*
 * Make sure that the dive references are intact:
 *  - all gas changes are connected to mixes
 *  - all pressures are connected to tanks (these are created if not
 *  found: this is not an error condition)
 *  - all tank gasses exist
 * Returns zero on failure, non-zero on success.
 */
static int
link_dive(struct dive *d)
{
	struct samp	*s;
	size_t		 i, j, errs = 0;

	TAILQ_FOREACH(s, &d->samps, entries) {
		if (SAMP_GASCHANGE & s->flags) {
			for (i = 0; i < d->gassz; i++)
				if (s->gaschange == d->gas[i].num)
					break;
			if (i == d->gassz) {
				warnx("unknown gas: %zu", 
					s->gaschange);
				errs++;
			}
		}
		for (i = 0; i < s->pressuresz; i++) {
			for (j = 0; j < d->cylsz; j++) 
				if (s->pressure[i].tank == 
				    d->cyls[j].num)
					break;
			if (j == d->cylsz) {
				d->cyls = reallocarray
					(d->cyls, d->cylsz + 1,
					 sizeof(struct cylinder));
				if (NULL == d->cyls)
					err(EXIT_FAILURE, NULL);
				memset(&d->cyls[d->cylsz], 0,
					sizeof(struct cylinder));
				d->cyls[d->cylsz++].num = 
					s->pressure[i].tank;
			}
		}
	}

	for (i = 0; i < d->cylsz; i++) {
		if (0 == d->cyls[i].mix)
			continue;
		for (j = 0; j < d->gassz; j++)
			if (d->cyls[i].mix == d->gas[j].num)
				break;
		if (j == d->gassz) {
			warnx("unknown gas: %zu", d->cyls[i].mix);
			errs++;
		}
	}

	return 0 == errs;
}

static int
link_dives(struct diveq *dq)
{
	size_t	 	 errs = 0;
	struct dive	*d;

	TAILQ_FOREACH(d, dq, entries)
		if ( ! link_dive(d))
			errs++;

	return 0 == errs;
}

/*
 * Parse a set of dives, accumulating the dives into "dq" and into the
 * group dives.
 * Dives in "dq" are ordered, by default, by date.
 * If a split is specified, however, they're ordered by relative date
 * from the first dive of the given group.
 * So for example, if you have GROUP_DATE and two days, the dives will
 * be ordered by the relative from the beginning of each day's first
 * dive.
 * This lets them be interleaved nicely.
 */
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
	else if ( ! link_dives(dq))
		ssz = -1;

	close(fd);
	free(pp.buf);
	return 0 == ssz;
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
				free(s->vendor.buf);
				free(s->events);
				free(s->pressure);
				free(s);
			}
			free(d->gas);
			free(d->cyls);
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
			free(dl->file);
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
	struct divestat *st, enum group group, enum groupsort sort)
{

	if (NULL == (*p = XML_ParserCreate(NULL)))
		err(EXIT_FAILURE, NULL);

	TAILQ_INIT(dq);
	memset(st, 0, sizeof(struct divestat));
	st->group = group;
	st->groupsort = sort;
	TAILQ_INIT(&st->dlogs);
}

void
divecmd_print_dive_tanks(FILE *f, const struct dive *d)
{
	size_t	 i;

	if (0 == d->cylsz)
		return;

	fputs("\t\t\t<tanks>\n", f);
	for (i = 0; i < d->cylsz; i++) {
		printf("\t\t\t\t<tank num=\"%zu\"", d->cyls[i].num);
		if (d->cyls[i].mix)
			printf(" gasmix=\"%zu\"", d->cyls[i].mix);
		if (d->cyls[i].size > FLT_EPSILON)
			printf(" volume=\"%g\"", d->cyls[i].size);
		if (d->cyls[i].workpressure > FLT_EPSILON)
			printf(" workpressure=\"%g\"", 
				d->cyls[i].workpressure);
		printf(" />\n");
	}
	fputs("\t\t\t</tanks>\n", f);
}

void
divecmd_print_dive_gasmixes(FILE *f, const struct dive *d)
{
	size_t	 i;

	if (0 == d->gassz)
		return;

	fputs("\t\t\t<gasmixes>\n", f);
	for (i = 0; i < d->gassz; i++) {
		printf("\t\t\t\t<gasmix num=\"%zu\"",
			d->gas[i].num);
		if (d->gas[i].o2 > FLT_EPSILON)
			printf(" o2=\"%g\"", d->gas[i].o2);
		if (d->gas[i].n2 > FLT_EPSILON)
			printf(" n2=\"%g\"", d->gas[i].n2);
		if (d->gas[i].he > FLT_EPSILON)
			printf(" he=\"%g\"", d->gas[i].he);
		printf(" />\n");
	}
	fputs("\t\t\t</gasmixes>\n", f);
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
	size_t	 i;

	fprintf(f, "\t\t\t\t<sample time=\"%zu\">\n", s->time);

	if (SAMP_DEPTH & s->flags)
		fprintf(f, "\t\t\t\t\t"
			"<depth value=\"%g\" />\n", s->depth);
	if (SAMP_TEMP & s->flags)
		fprintf(f, "\t\t\t\t\t"
		        "<temp value=\"%g\" />\n", s->temp);
	if (SAMP_GASCHANGE & s->flags)
		fprintf(f, "\t\t\t\t\t"
		        "<gaschange mix=\"%zu\" />\n", s->gaschange - 1);
	if (SAMP_RBT & s->flags)
		fprintf(f, "\t\t\t\t\t"
		        "<rbt value=\"%zu\" />\n", s->rbt);
	for (i = 0; i < s->pressuresz; i++) 
		fprintf(f, "\t\t\t\t\t"
		        "<pressure value=\"%g\" tank=\"%zu\" />\n", 
			s->pressure[i].pressure, s->pressure[i].tank);
	for (i = 0; i < s->eventsz; i++)  {
		if ((EVENT_gaschange == s->events[i].type ||
		     EVENT_gaschange2 == s->events[i].type) &&
		    s->events[i].flags > 0) {
			fprintf(f, "\t\t\t\t\t<gaschange mix=\"%u\" />\n",
				s->events[i].flags - 1);
			continue;
		}
		fprintf(f, "\t\t\t\t\t<event type=\"%s\"", 
			events[s->events[i].type]);
		if (s->events[i].flags)
			fprintf(f, " flags=\"%u\"", 
				s->events[i].flags);
		if (s->events[i].duration)
			fprintf(f, " duration=\"%zu\"", 
				s->events[i].duration);
		fprintf(f, " />\n");
	}
	if (SAMP_DECO & s->flags) {
		if (DECO_ndl != s->deco.type) {
			fprintf(f, "\t\t\t\t\t"
				"<deco type=\"%s\"",
				decos[s->deco.type]);
			if (s->deco.depth > FLT_EPSILON)
				printf(" depth=\"%g\"",
					s->deco.depth);
			if (s->deco.duration > 0)
				printf(" duration=\"%zu\"",
					s->deco.duration);
			puts(" />");
		} else {
			fprintf(f, "\t\t\t\t\t"
				"<deco type=\"%s\"",
				decos[s->deco.type]);
			if (s->deco.duration > 0)
				printf(" duration=\"%zu\"",
					s->deco.duration);
			puts(" />");
		}
	}
	if (SAMP_VENDOR & s->flags)
		fprintf(f, "\t\t\t\t\t"
			"<vendor type=\"%zu\">%s</vendor>\n",
			s->vendor.type, s->vendor.buf);
	if (SAMP_CNS & s->flags)
		fprintf(f, "\t\t\t\t\t"
			"<cns value=\"%.2f\" />\n", s->cns);
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
		"<divelog program=\"dcmdfind\" "
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
	divecmd_print_dive_gasmixes(f, d);
	divecmd_print_dive_tanks(f, d);
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
