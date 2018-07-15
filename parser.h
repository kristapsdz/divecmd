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
	GROUP_DATE,
	GROUP_DIVELOG
};

enum	groupsort {
	GROUPSORT_DATETIME,
	GROUPSORT_MAXDEPTH,
	GROUPSORT_MAXTIME,
	GROUPSORT_RMAXDEPTH,
	GROUPSORT_RMAXTIME
};

enum	event {
	EVENT_none,
	EVENT_decostop,
	EVENT_rbt,
	EVENT_ascent,
	EVENT_ceiling,
	EVENT_workload,
	EVENT_transmitter,
	EVENT_violation,
	EVENT_bookmark,
	EVENT_surface,
	EVENT_safetystop,
	EVENT_gaschange,
	EVENT_safetystop_voluntary,
	EVENT_safetystop_mandatory,
	EVENT_deepstop,
	EVENT_ceiling_safetystop,
	EVENT_floor,
	EVENT_divetime,
	EVENT_maxdepth,
	EVENT_olf,
	EVENT_po2,
	EVENT_airtime,
	EVENT_rgbm,
	EVENT_heading,
	EVENT_tissuelevel,
	EVENT_gaschange2,
	EVENT__MAX
};

enum	deco {
	DECO_ndl,
	DECO_safetystop,
	DECO_decostop,
	DECO_deepstop,
	DECO__MAX
};

struct	sampevent {
	size_t	  	 duration;
	unsigned int	 bits;
};

struct	sampdeco {
	double	  	 depth;
	enum deco 	 type;
	size_t	  	 duration;
};

struct	sampvendor {
	char		*buf;
	size_t		 type;
};

struct	samppres {
	/* FIXME: divegas or tank number? */
	size_t		 tank; /* divegas num */
	double		 pressure; /* bar */
};

/*
 * A sample within a dive profile.
 * The "flags" field (which may be zero) dictates which are the
 * available records within the sample.
 */
struct	samp {
	size_t		  time; /* seconds since start */
	double		  depth; /* metres */
	double		  temp; /* celsius */
	struct samppres	  pressure; /* bar */
	size_t		  rbt; /* seconds */
	size_t		  gaschange; /* num of gas change */
	struct sampevent *events;
	size_t		  eventsz;
	unsigned int	  flags; /* bits of 1u << "enum event" */
	struct sampdeco	  deco;
	struct sampvendor vendor;
#define	SAMP_DEPTH	  0x01
#define	SAMP_TEMP	  0x02
#define	SAMP_RBT	  0x04
#define	SAMP_EVENT	  0x08
#define	SAMP_DECO	  0x10
#define	SAMP_VENDOR	  0x20
#define	SAMP_GASCHANGE	  0x40
#define	SAMP_PRESSURE	  0x80
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

/*
 * A self-contained divelog article.
 * This is parsed from the <divelog> element.
 */
struct	dlog {
	char		 *file; /* file or <stdin> */
	size_t		  line; /* parse line */
	char		 *ident; /* diver or NULL */
	char		 *program; /* program or NULL */
	char		 *vendor; /* vendor or NULL */
	char		 *product; /* product or NULL */
	char		 *model; /* model or NULL */
	TAILQ_ENTRY(dlog) entries;
};

TAILQ_HEAD(dlogq, dlog);

struct	divegas {
	double		 o2; /* O2 or 0 if unset */
	double		 n2; /* N2 or 0 if unset */
	double		 he; /* He or 0 if unset */
	size_t		 num;
};

struct	dive {
	size_t		     pid; /* unique in parse sequence */
	size_t		     line; /* parse line */
	time_t		     datetime; /* time or zero */
	size_t		     num; /* number or zero */
	size_t		     duration; /* duration or zero */
	enum mode	     mode; /* dive mode */
	struct sampq	     samps; /* samples */
	struct divegas	    *gas;
	size_t		     gassz;
	double		     maxdepth; /* maximum sample depth */
	int		     hastemp; /* do we have temps? */
	double		     maxtemp; /* maximum (hottest) temp */
	double		     mintemp; /* minimum (coldest) temp */
	size_t		     maxtime; /* maximum sample time */
	size_t		     nsamps; /* number of samples */
	char		    *fprint; /* fingerprint or NULL */
	struct dgroup 	    *group; /* group identifier */
	const struct dlog   *log; /* source divelog */
	TAILQ_ENTRY(dive)    entries; /* in-dive entry */
	TAILQ_ENTRY(dive)    gentries; /* in-group entry */
};

struct	divestat {
	double		  maxdepth; /* maximum over all dives */
	time_t		  timestamp_min; /* minimum timestamp */
	time_t		  timestamp_max; /* maximum timestamp */
	enum group	  group; /* how we're grouping dives */
	enum groupsort	  groupsort; /* how we're sorting dives */
	struct dgroup	**groups; /* all groups */
	size_t		  groupsz; /* size of "groups" */
	struct dlogq	  dlogs; /* all divelog nodes */
};

__BEGIN_DECLS

void	 divecmd_init(XML_Parser *, struct diveq *, 
		struct divestat *, enum group, enum groupsort);
void	 divecmd_free(struct diveq *, struct divestat *);
int	 divecmd_parse(const char *, XML_Parser, 
		struct diveq *dq, struct divestat *);

void	 divecmd_print_diveq_close(FILE *);
void	 divecmd_print_diveq_open(FILE *);
void	 divecmd_print_dive(FILE *, const struct dive *);
void	 divecmd_print_dive_close(FILE *);
void	 divecmd_print_dive_fingerprint(FILE *, const struct dive *);
void	 divecmd_print_dive_gasmixes(FILE *, const struct dive *);
void	 divecmd_print_dive_open(FILE *, const struct dive *);
void	 divecmd_print_dive_sampleq(FILE *, const struct sampq *);
void	 divecmd_print_dive_sampleq_close(FILE *);
void	 divecmd_print_dive_sampleq_open(FILE *);
void	 divecmd_print_dive_sample(FILE *, const struct samp *);
void	 divecmd_print_close(FILE *);
void	 divecmd_print_open(FILE *, const struct dlog *);

extern int verbose;

__END_DECLS

#endif /* !PARSER_H */
