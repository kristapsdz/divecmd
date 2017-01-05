#ifdef __OpenBSD_
# include <sys/param.h>
#endif
#include <sys/queue.h>

#include <err.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <expat.h>

struct	dive {
	size_t		  num;
	TAILQ_ENTRY(dive) entries;
};

TAILQ_HEAD(diveq, dive);

struct	parse {
	XML_Parser	 p; /* parser routine */
	const char	*file; /* parsed filename */
	struct dive	*curdive; /* current dive */
	struct diveq	*dives;
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
	const XML_Char	**attp;

	if (0 == strcmp(s, "dive")) {
		p->curdive = calloc(1, sizeof(struct dive));
		if (NULL == p->curdive)
			err(EXIT_FAILURE, NULL);
		TAILQ_INSERT_TAIL(p->dives, p->curdive, entries);
		for (attp = atts; NULL != *attp; attp += 2) {
			if (0 == strcmp(attp[0], "number"))
				p->curdive->num = atoi(attp[1]);
		}
	}
}

static void
parse_close(void *dat, const XML_Char *s)
{
	struct parse	*p = dat;

	if (0 == strcmp(s, "dive"))
		p->curdive = NULL;
}

static int
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

int
main(int argc, char *argv[])
{
	int		 c, rc = 0;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct dive	*d;

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while (-1 != (c = getopt(argc, argv, "")))
		goto usage;

	argc -= optind;
	argv += optind;

	if (NULL == (p = XML_ParserCreate(NULL)))
		err(EXIT_FAILURE, NULL);

	TAILQ_INIT(&dq);

	if (0 == argc)
		rc = parse("-", p, &dq);
	for (i = 0; i < (size_t)argc; i++)
		if ( ! (rc = parse(argv[i], p, &dq)))
			break;

	XML_ParserFree(p);

	/* Narrow the pledge to just stdio. */

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	while ( ! TAILQ_EMPTY(&dq)) {
		d = TAILQ_FIRST(&dq);
		TAILQ_REMOVE(&dq, d, entries);
		free(d);
	}

	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [file]\n", getprogname());
	return(EXIT_FAILURE);
}
