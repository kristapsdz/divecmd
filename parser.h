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
#ifndef PARSER_H
#define PARSER_H

enum	mode {
	MODE_NONE,
	MODE_FREEDIVE,
	MODE_GAUGE,
	MODE_OC,
	MODE_CC
};

enum	group {
	GROUP_NONE,
	GROUP_DIVER,
	GROUP_DATE
};

struct	samp {
	size_t		  time; /* seconds since start */
	double		  depth; /* metres */
	double		  temp; /* celsius */
	unsigned int	  flags; /* sample contents */
#define	SAMP_DEPTH	  0x01
#define	SAMP_TEMP	  0x02
	TAILQ_ENTRY(samp) entries;
};

TAILQ_HEAD(sampq, samp);

struct	dive;

TAILQ_HEAD(diveq, dive);

struct	dgroup {
	char		 *name; /* date (or NULL) */
	time_t		  mintime; /* minimum date in group */
	size_t		  id; /* unique identifier */
	size_t		  ndives; /* number of dives in queue */
	struct diveq	  dives; /* all dives */
};

struct	dlog {
	char		 *ident; /* diver or NULL */
	TAILQ_ENTRY(dlog) entries;
};

TAILQ_HEAD(dlogq, dlog);

struct	dive {
	time_t		     datetime; /* time or zero */
	size_t		     num; /* number */
	enum mode	     mode; /* dive mode */
	size_t		     duration; /* or zero */
	struct sampq	     samps; /* samples */
	double		     maxdepth; /* maximum sample depth */
	int		     hastemp; /* do we have temps? */
	double		     maxtemp; /* maximum (hottest) temp */
	double		     mintemp; /* minimum (coldest) temp */
	size_t		     maxtime; /* maximum sample time */
	size_t		     nsamps; /* number of samples */
	const struct dgroup *group; /* group identifier */
	const struct dlog   *log; /* source divelog */
	TAILQ_ENTRY(dive)    entries;
	TAILQ_ENTRY(dive)    gentries;
};

struct	divestat {
	double		  maxdepth; /* maximum over all dives */
	time_t		  timestamp_min; /* minimum timestamp */
	time_t		  timestamp_max; /* maximum timestamp */
	enum group	  group; /* how we're grouping dives */
	struct dgroup	**groups; /* all groups */
	size_t		  groupsz; /* size of "groups" */
	struct dlogq	  dlogs; /* all divelog nodes */
};

__BEGIN_DECLS

void	 parse_init(XML_Parser *, struct diveq *, 
		struct divestat *, enum group);

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
int	 parse(const char *, XML_Parser, 
		struct diveq *dq, struct divestat *);
void	 parse_free(struct diveq *, struct divestat *);

extern int verbose;

__END_DECLS

#endif /* !PARSER_H */
