.SUFFIXES: .1.html .1 .xml .rest.pdf .restscatter.pdf .summary.pdf .png .pdf .stack.pdf .aggr.pdf .scatter.pdf .aggrtemp.pdf .all.pdf
.PHONY: clean

include Makefile.configure

VERSION		 = 0.0.15
LDADD		+= -ldivecomputer
CFLAGS		+= -DVERSION="\"$(VERSION)\""
GROFF		?= groff

OBJS		 = common.o \
		   main.o \
		   download.o \
		   list.o \
		   xml.o
BINOBJS		 = divecmd2csv.o \
		   divecmd2divecmd.o \
		   divecmd2grap.o \
		   divecmd2list.o \
		   divecmd2json.o \
		   divecmd2term.o \
		   parser.o \
		   ssrf2divecmd.o
PREBINS		 = divecmd2pdf.in \
		   divecmd2ps.in
BINS		 = dcmd \
		   dcmd2csv \
		   dcmdfind \
		   dcmd2grap \
		   dcmdls \
		   dcmd2json \
		   dcmdterm \
		   dcmd2pdf \
		   dcmd2ps \
		   ssrf2divecmd
MAN1S		 = dcmd.1 \
		   dcmd2csv.1 \
		   dcmdfind.1 \
		   dcmd2grap.1 \
		   dcmdls.1 \
		   dcmd2json.1 \
		   dcmd2pdf.1 \
		   dcmd2ps.1 \
		   dcmdterm.1
PNGS		 = daily.aggr.png \
		   daily.aggrtemp.png \
		   daily.rest.png \
		   daily.restscatter.png \
		   daily.scatter.png \
		   daily.stack.png \
		   daily.summary.png \
		   daily.temp.png \
		   multiday.restscatter.png \
		   multiday.rsummary.png \
		   multiday.stack.png \
		   short.stack.png
PDFS		 = daily.aggr.pdf \
		   daily.aggrtemp.pdf \
		   daily.all.pdf \
		   daily.rest.pdf \
		   daily.restscatter.pdf \
		   daily.scatter.pdf \
		   daily.stack.pdf \
		   daily.summary.pdf \
		   daily.temp.pdf \
		   multiday.restscatter.pdf \
		   multiday.rsummary.pdf \
		   multiday.stack.pdf \
		   short.stack.pdf
HTMLS		 = dcmd.1.html \
		   dcmd2csv.1.html \
		   dcmdfind.1.html \
		   dcmd2grap.1.html \
		   dcmdls.1.html \
		   dcmd2json.1.html \
		   dcmd2pdf.1.html \
		   dcmd2ps.1.html \
		   dcmdterm.1.html \
		   index.html
CSSS		 = index.css \
		   mandoc.css
WWWDIR		 = /var/www/vhosts/kristaps.bsd.lv/htdocs/divecmd
XMLS		 = daily.xml \
		   day1.xml \
		   day2.xml \
		   multiday.xml \
		   temperature.xml
BUILT		 = screenshot1.png \
		   screenshot2.png \
		   slider.js

all: $(BINS) 

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	install -m 0755 $(BINS) $(DESTDIR)$(BINDIR)
	install -m 0444 $(MAN1S) $(DESTDIR)$(MANDIR)/man1

dcmd: $(OBJS) compats.o
	$(CC) $(CPPFLAGS) -o $@ $(OBJS) compats.o $(LDFLAGS) $(LDADD)

dcmdfind: divecmd2divecmd.o parser.o compats.o
	$(CC) $(CPPFLAGS) -o $@ divecmd2divecmd.o parser.o compats.o -lexpat

ssrf2divecmd: ssrf2divecmd.o parser.o compats.o
	$(CC) $(CPPFLAGS) -o $@ ssrf2divecmd.o parser.o compats.o -lexpat

dcmdterm: divecmd2term.o parser.o compats.o
	$(CC) $(CPPFLAGS) -o $@ divecmd2term.o parser.o compats.o -lexpat -lm

dcmd2grap: divecmd2grap.o parser.o compats.o
	$(CC) $(CPPFLAGS) -o $@ divecmd2grap.o parser.o compats.o -lexpat

dcmdls: divecmd2list.o parser.o compats.o
	$(CC) $(CPPFLAGS) -o $@ divecmd2list.o parser.o compats.o -lexpat

dcmd2json: divecmd2json.o parser.o compats.o
	$(CC) $(CPPFLAGS) -o $@ divecmd2json.o parser.o compats.o -lexpat

dcmd2csv: divecmd2csv.o parser.o compats.o
	$(CC) $(CPPFLAGS) -o $@ divecmd2csv.o parser.o compats.o -lexpat

dcmd2pdf: divecmd2pdf.in
	sed "s!@GROFF@!$(GROFF)!g" divecmd2pdf.in >$@

dcmd2ps: divecmd2pdf.in
	sed "s!@GROFF@!$(GROFF)!g" divecmd2ps.in >$@

$(OBJS): extern.h config.h

compats.o: config.h

$(BINOBJS): parser.h config.h

clean:
	rm -f $(OBJS) compats.o $(BINS) $(BINOBJS) $(HTMLS) $(PDFS) $(PNGS)
	rm -f divecmd.tar.gz divecmd.tar.gz.sha512

distclean: clean
	rm -f Makefile.configure config.h config.log

#######################################################################
# All of these rules are used for the www target.
#######################################################################

www: $(HTMLS) $(PDFS) $(PNGS) divecmd.tar.gz divecmd.tar.gz.sha512

installwww: www
	mkdir -p $(WWWDIR)
	mkdir -p $(WWWDIR)/snapshots
	install -m 0444 $(CSSS) $(HTMLS) $(PDFS) $(PNGS) $(XMLS) $(BUILT) $(WWWDIR)
	install -m 0444 divecmd.tar.gz $(WWWDIR)/snapshots/divecmd-$(VERSION).tar.gz
	install -m 0444 divecmd.tar.gz.sha512 $(WWWDIR)/snapshots/divecmd-$(VERSION).tar.gz.sha512
	install -m 0444 divecmd.tar.gz $(WWWDIR)/snapshots
	install -m 0444 divecmd.tar.gz.sha512 $(WWWDIR)/snapshots

$(PNGS): $(PDFS)

$(PDFS): dcmd2grap dcmdfind

divecmd.tar.gz:
	mkdir -p .dist/divecmd-$(VERSION)/
	install -m 0755 configure .dist/divecmd-$(VERSION)
	install -m 0644 *.c *.h Makefile *.1 $(PREBINS) .dist/divecmd-$(VERSION)
	( cd .dist/ && tar zcf ../$@ ./ )
	rm -rf .dist/

divecmd.tar.gz.sha512: divecmd.tar.gz
	sha512 divecmd.tar.gz >$@

index.html: index.xml
	sblg -t index.xml -o- versions.xml | \
		sed "s!@VERSION@!$(VERSION)!g" >$@

.1.1.html:
	mandoc -Thtml -Ostyle=mandoc.css $< >$@

.xml.summary.pdf:
	./dcmd2grap -m summary $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.all.pdf:
	./dcmd2grap -a -m all $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.restscatter.pdf:
	./dcmd2grap -m restscatter $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

multiday.restscatter.pdf: day1.xml day2.xml
	./dcmd2grap -s date -m restscatter day1.xml day2.xml | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.rest.pdf:
	./dcmd2grap -m rest $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.stack.pdf:
	./dcmd2grap -m stack $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

short.stack.pdf: multiday.xml
	./dcmd2grap -s date -d -m stack multiday.xml | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.aggr.pdf:
	./dcmd2grap -m aggr $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

daily.aggrtemp.pdf: temperature.xml
	./dcmd2grap -m aggrtemp temperature.xml | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

daily.temp.pdf: temperature.xml
	./dcmdfind -s temperature.xml | ./dcmd2grap -am temp | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

multiday.stack.pdf: multiday.xml
	./dcmd2grap -s date -m stack multiday.xml | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

multiday.rsummary.pdf: day1.xml day2.xml
	./dcmd2grap -s date -m rsummary day1.xml day2.xml | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.scatter.pdf:
	./dcmd2grap -m scatter $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.pdf.png:
	convert -density 120 $< -flatten -trim $@
