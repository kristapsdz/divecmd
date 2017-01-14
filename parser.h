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

struct	dive {
	time_t		  datetime; /* time or zero */
	size_t		  num; /* number */
	enum mode	  mode; /* dive mode */
	size_t		  duration; /* or zero */
	struct sampq	  samps; /* samples */
	double		  maxdepth; /* maximum sample depth */
	size_t		  maxtime; /* maximum sample time */
	size_t		  nsamps; /* number of samples */
	TAILQ_ENTRY(dive) entries;
};

TAILQ_HEAD(diveq, dive);

__BEGIN_DECLS

int	 parse(const char *, XML_Parser, struct diveq *);
void	 parse_free(struct diveq *);

extern int verbose;

__END_DECLS

#endif /* !PARSER_H */
