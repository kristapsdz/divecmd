.SUFFIXES: .1.html .1 .xml .rest.pdf .restscatter.pdf .summary.pdf .png .pdf .stack.pdf .aggr.pdf .scatter.pdf
.PHONY: clean

PREFIX 		 = /usr/local
VERSION		 = 0.0.1
LDFLAGS		+= -L/usr/local/lib -ldivecomputer
CFLAGS		+= -g -I/usr/local/include -W -Wall -DVERSION="\"$(VERSION)\""
OBJS		 = common.o \
		   main.o \
		   download.o \
		   list.o \
		   xml.o
BINOBJS		 = divecmd2grap.o \
		   divecmd2json.o \
		   divecmd2term.o \
		   parser.o
BINDIR 		 = $(PREFIX)/bin
MANDIR 		 = $(PREFIX)/man
BINS		 = divecmd \
		   divecmd2grap \
		   divecmd2json \
		   divecmd2term
MAN1S		 = divecmd.1 \
		   divecmd2grap.1 \
		   divecmd2json.1 \
		   divecmd2term.1
PNGS		 = daily.aggr.png \
		   daily.rest.png \
		   daily.restscatter.png \
		   daily.scatter.png \
		   daily.stack.png \
		   daily.summary.png
PDFS		 = daily.aggr.pdf \
		   daily.rest.pdf \
		   daily.restscatter.pdf \
		   daily.scatter.pdf \
		   daily.stack.pdf \
		   daily.summary.pdf
HTMLS		 = divecmd.1.html \
		   divecmd2grap.1.html \
		   divecmd2json.1.html \
		   divecmd2term.1.html \
		   index.html
CSSS		 = mandoc.css
WWWDIR		 = /var/www/vhosts/kristaps.bsd.lv/htdocs/divecmd

all: $(BINS)

www: $(HTMLS) $(PDFS) $(PNGS)

installwww: www
	mkdir -p $(WWWDIR)
	install -m 0444 $(CSSS) $(HTMLS) $(WWWDIR)

divecmd: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) 

divecmd2term: divecmd2term.o parser.o
	$(CC) -o $@ divecmd2term.o parser.o -lexpat -lm

divecmd2grap: divecmd2grap.o parser.o
	$(CC) -o $@ divecmd2grap.o parser.o -lexpat

divecmd2json: divecmd2json.o parser.o
	$(CC) -o $@ divecmd2json.o parser.o -lexpat

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	install -m 0755 $(BINS) $(DESTDIR)$(BINDIR)
	install -m 0444 $(MAN1S) $(DESTDIR)$(MANDIR)/man1

$(PNGS): $(PDFS)

$(PDFS): divecmd2grap

$(OBJS): extern.h

$(BINOBJS): parser.h

index.html: index.xml
	sed "s!@VERSION@!$(VERSION)!g" index.xml >$@

clean:
	rm -f $(OBJS) $(BINS) $(BINOBJS) $(HTMLS) $(PDFS) $(PNGS)

.1.1.html:
	mandoc -Thtml -Ostyle=mandoc.css $< >$@

.xml.summary.pdf:
	./divecmd2grap -m summary $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.restscatter.pdf:
	./divecmd2grap -m restscatter $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.rest.pdf:
	./divecmd2grap -m rest $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.stack.pdf:
	./divecmd2grap -m stack $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.aggr.pdf:
	./divecmd2grap -m aggr $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.scatter.pdf:
	./divecmd2grap -m scatter $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.pdf.png:
	convert -density 120 $< -flatten -trim $@
