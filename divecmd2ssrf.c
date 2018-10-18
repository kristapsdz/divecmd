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
#include <float.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <expat.h>

#include "parser.h"

int verbose = 0;

static	const char *const events[EVENT__MAX] = {
	"none",			/* EVENT_none */
	"deco stop",		/* EVENT_decostop */
	"rbt",			/* EVENT_rbt */
	"ascent",		/* EVENT_ascent */
	"ceiling",		/* EVENT_ceiling */
	"workload",		/* EVENT_workload */
	"transmitter",		/* EVENT_transmitter */
	"violation",		/* EVENT_violation */
	"bookmark",		/* EVENT_bookmark */
	"surface",		/* EVENT_surface */
	"safety stop",		/* EVENT_safetystop */
	"gaschange",		/* EVENT_gaschange */
	"safety stop (voluntary)", /* EVENT_safetystop_voluntary */
	"safety stop (mandatory)", /* EVENT_safetystop_mandatory */
	"deepstop",		/* EVENT_deepstop */
	"ceiling (safety stop)", /* EVENT_ceiling_safetystop */
	"below floor",		/* EVENT_floor */
	"divetime",		/* EVENT_divetime */
	"maxdepth",		/* EVENT_maxdepth */
	"OLF",			/* EVENT_olf */
	"pO₂",			/* EVENT_po2 */
	"airtime",		/* EVENT_airtime */
	"rgbm",			/* EVENT_rgbm */
	"heading",		/* EVENT_heading */
	"tissue level warning",	/* EVENT_tissuelevel */
	"gaschange",		/* EVENT_gaschange2 */
};

static void
print_deco(const struct sampdeco *d)
{

	/* NDL only has duration (if that). */

	if (DECO_ndl == d->type && d->duration) 
		printf(" ndl='%zu:%.2zu min'",
			d->duration / 60, d->duration % 60);
	if (DECO_ndl == d->type)
		return;
	if (d->duration)
		printf(" stoptime='%zu:%.2zu min'",
			d->duration / 60, d->duration % 60);
	if (d->depth > FLT_EPSILON)
		printf(" stopdepth='%.1f m'", d->depth);
}

/*
 * Have a special function to print a gas change.
 * This is because we want to link the gas change mixture to a mixture
 * we already have, then to the cylinder we're printing.
 */
static void
print_evtgas(size_t mix, const struct dive *d, 
	const size_t *map_gas, const struct samp *s)
{
	size_t	 i;

	printf("    <event time='%zu:%.2zu min' "
	       "type='25' name='gaschange'", 
	       s->time / 60, s->time % 60);
	for (i = 0; i < d->gassz; i++)
		if (d->gas[i].num == mix)
			break;
	if (i == d->gassz)
		errx(EXIT_FAILURE, "%s:%zu:%zu: gas mix "
			"not found: %zu", d->log->file,
			s->line, s->col, mix);
	assert(i < d->gassz);
	printf(" flags='%zu' cylinder='%zu' />\n", 
		map_gas[i] + 1, map_gas[i]);
}

static void
print_evt(const struct sampevent *p, const struct samp *s, 
	const struct dive *d, const size_t *map_gas)
{
	/*
	 * Process the gaschange2 event as an actual gas change using
	 * print_evtgas().
	 * To do so, we actually need a mixture, which in the flags is
	 * non-zero.
	 * Old versions of dcmd didn't process this properly and didn't
	 * print out the flags.
	 */

	if (EVENT_gaschange2 == p->type) {
		if (0 == p->flags)
			warnx("%s:%zu:%zu: omitting "
				"gas change without mix",
				d->log->file, s->line, s->col);
		else
			print_evtgas(p->flags - 1, d, map_gas, s);
		return;
	}
	fputs("    ", stdout);
	printf("<event time='%zu:%.2zu min' type='%u' name='%s'",
		s->time / 60, s->time % 60, p->type, events[p->type]);
	if (p->flags)
		printf(" flags='%u'", p->flags);
	puts(" />");
}

static void
print_gas(const struct divegas *g)
{

	printf("   <cylinder");
	if (g->o2 > FLT_EPSILON)
		printf(" o2='%.1f%%'", g->o2);
	if (g->n2 > FLT_EPSILON)
		printf(" n2='%.1f%%'", g->n2);
	if (g->he > FLT_EPSILON)
		printf(" he='%.1f%%'", g->he);
	puts(" />");
}

static void
print_cylinder(const struct cylinder *p, const struct dive *d)
{
	size_t	 		 i;
	const struct divegas	*g;

	printf("   <cylinder");
	if (p->size > FLT_EPSILON)
		printf(" size='%.1f l'", p->size);
	if (p->workpressure > FLT_EPSILON)
		printf(" workpressure='%.1f bar'", p->workpressure);
	if (p->mix) {
		for (i = 0; i < d->gassz; i++)
			if (d->gas[i].num == p->mix)
				break;
		if (i == d->gassz)
			errx(EXIT_FAILURE, "%s: gas mix "
				"corresponding to cylinder "
				"not found", d->log->file);
		g = &d->gas[i];
		if (g->o2 > FLT_EPSILON)
			printf(" o2='%.1f%%'", g->o2);
		if (g->n2 > FLT_EPSILON)
			printf(" n2='%.1f%%'", g->n2);
		if (g->he > FLT_EPSILON)
			printf(" he='%.1f%%'", g->he);
	}
	puts(" />");
}

static void
print_all(const struct dlog *dl, const struct diveq *dq)
{
	const struct dive *d;
	const struct samp *s;
	struct tm	  *tm;
	size_t		   i, j, cylsz;
	size_t		  *map_cyl, *map_gas;
	unsigned int	   bits;
	int		   in_deco;

	printf("<divelog program=\'dcmd2ssrf\' "
	       " version=\'" VERSION "\'>\n"
	       " <settings>\n"
	       "  <divecomputerid model=\'%s %s\'/>\n"
	       " </settings>\n"
	       " <divesites>\n"
	       " </divesites>\n"
	       " <dives>\n",
	       dl->vendor, dl->product);

	TAILQ_FOREACH(d, dq, entries) {
		/* Start with the "dive" information. */

		printf("  <dive number='%zu'", d->num);
		if (d->datetime) {
			tm = localtime(&d->datetime);
			printf(" date='%.4d-%.2d-%.2d'"
			       " time='%.2d:%.2d:%.2d'",
			       tm->tm_year + 1900, 
			       tm->tm_mon + 1, tm->tm_mday,
			       tm->tm_hour, tm->tm_min,
			       tm->tm_sec);
		}
		if (d->duration)
			printf(" duration='%zu:%.2zu min'",
				d->duration / 60,
				d->duration % 60);
		if (NULL != d->fprint) 
			printf(" diveid='%s'", d->fprint);
		puts(">");

		/*
		 * In Subsurface, we have "cylinders" that unify both
		 * tanks and gas mixes.
		 * For us, they're different, although tanks can refer
		 * to gas mixes.
		 * Moreover, the cylinders are laid out in order, so gas
		 * changes (and pressures) refer to cylinders by their
		 * defined order.
		 */

		map_cyl = map_gas = NULL;

		if (d->cylsz && NULL == 
		    (map_cyl = calloc(d->cylsz, sizeof(size_t))))
			err(EXIT_FAILURE, NULL);
		for (cylsz = i = 0; i < d->cylsz; i++) {
			print_cylinder(&d->cyls[i], d);
			map_cyl[i] = cylsz++;
		}

		if (d->gassz && NULL == 
		    (map_gas = calloc(d->gassz, sizeof(size_t))))
			err(EXIT_FAILURE, NULL);
		for (i = 0; i < d->gassz; i++) {
			for (j = 0; j < d->cylsz; j++)
				if (d->gas[i].num == d->cyls[j].mix)
					break;
			if (j == d->cylsz)
				print_gas(&d->gas[i]);
			map_gas[i] = j < d->cylsz ? j : cylsz++;
		}

		/* The divecomputer wraps the samples. */

		printf("   <divecomputer model='%s %s'",
			d->log->vendor, d->log->product);
		if (MODE_FREEDIVE == d->mode) 
			printf(" dctype='Freedive'");
		else if (MODE_CC == d->mode)
			printf(" dctype='CCR'");
		puts(">");

		in_deco = 0;
		TAILQ_FOREACH(s, &d->samps, entries) {
			printf("    <sample time='%zu:%.2zu min'",
				s->time / 60, s->time % 60);
			bits = s->flags;
			if (SAMP_DEPTH & bits) {
				printf(" depth='%.1f m'", s->depth);
				bits &= ~SAMP_DEPTH;
			}
			if (SAMP_TEMP & bits) {
				printf(" temp='%.1f C'", s->temp);
				bits &= ~SAMP_TEMP;
			}
			if (SAMP_RBT & bits) {
				printf(" rbt='%zu:%.2zu min'", 
					s->rbt / 60, s->rbt % 60);
				bits &= ~SAMP_RBT;
			}
			if (SAMP_CNS & bits) {
				printf(" cns='%.0f%%'", 100.0 * s->cns);
				bits &= ~SAMP_CNS;
			}
			if (SAMP_DECO & bits) {
				/*
				 * Deco is complicated.
				 * Don't allow nested deco (e.g., a deep
				 * stop in a deco stop...?).
				 * When we have a NDL message and we're
				 * in deco, take that to mean that we're
				 * no longer in deco.
				 */

				print_deco(&s->deco);
				if (in_deco && 
				    (DECO_ndl == s->deco.type ||
				     DECO_safetystop == s->deco.type)) {
					printf(" in_deco='0'");
					in_deco = 0;
				} 
				if (in_deco && 
				    in_deco != s->deco.type)
					warnx("%s:%zu:%zu: cannot "
						"have nested deco",
						d->log->file,
						s->line, s->col);
				if (0 == in_deco && 
				    DECO_decostop == s->deco.type) {
					printf(" in_deco='1'");
					in_deco = DECO_decostop;
				}
				if (0 == in_deco && 
				    DECO_deepstop == s->deco.type) {
					printf(" in_deco='1'");
					in_deco = DECO_deepstop;
				}
				bits &= ~SAMP_DECO;
			} else if (in_deco) {
				/*warnx("%s:%zu:%zu: implicit end "
					"of deco interval",
					d->log->file, s->line, s->col);*/
				in_deco = 0;
			}


			puts(" />");

			for (i = 0; i < s->eventsz; i++)
				print_evt(&s->events[i], s, d, map_gas);
			if (SAMP_GASCHANGE & bits) {
				print_evtgas(s->gaschange, d, map_gas, s);
				bits &= ~SAMP_GASCHANGE;
			}

			/* Ignore vendor bits. */

			if (SAMP_VENDOR & bits) 
				bits &= ~SAMP_VENDOR;

			/* Make sure we get everything. */

			if (bits)
				warnx("%s:%zu:%zu: unhandled "
					"sample data", d->log->file, 
					s->line, s->col);
		}

		puts("   </divecomputer>\n"
		     "  </dive>");

		free(map_cyl);
		free(map_gas);
	}

	puts(" </dives>\n"
	     "</divelog>");
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 1;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct divestat	 st;

#if HAVE_PLEDGE
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif
	while (-1 != (c = getopt(argc, argv, "v")))
		switch (c) {
		case ('v'):
			verbose = 1;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	divecmd_init(&p, &dq, &st, 
		GROUP_DIVELOG, GROUPSORT_DATETIME);

	if (0 == argc)
		rc = divecmd_parse("-", p, &dq, &st);
	for (i = 0; i < (size_t)argc; i++)
		if ( ! (rc = divecmd_parse(argv[i], p, &dq, &st)))
			break;

#if HAVE_PLEDGE
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	XML_ParserFree(p);

	if ( ! rc)
		goto out;

	if (TAILQ_EMPTY(&dq)) {
		warnx("no dives to display");
		goto out;
	} else if (st.groupsz > 1) {
		warnx("only one computer/diver allowed");
		goto out;
	}

	print_all(TAILQ_FIRST(&st.dlogs), &dq);
out:
	divecmd_free(&dq, &st);
	return rc ? EXIT_SUCCESS : EXIT_FAILURE;
usage:
	fprintf(stderr, "usage: %s [-v] [file]\n", getprogname());
	return EXIT_FAILURE;
}
