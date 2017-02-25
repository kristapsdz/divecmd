## Synopsis

*divecmd* drives [libdivecomputer](http://www.libdivecomputer.org) to
extract the contents of your dive computer in a safe, simple manner.  It
has several programs: 
[divecmd(1)](https://kristaps.bsd.lv/divecmd/divecmd.1.html), which
downloads, parses, and formats dives;
[divecmd2term(1)](https://kristaps.bsd.lv/divecmd/divecmd2term.1.html),
which graphs formatted dives in an ANSI/VT100 terminal;
[divecmd2grap(1)](https://kristaps.bsd.lv/divecmd/divecmd2grap.1.html),
which graphs using *grap(1)*; and 
[divecmd2json(1)](https://kristaps.bsd.lv/divecmd/divecmd2json.1.html),
which exports dive profiles to the JSON format.

There are also some supporting utilities, such as
[divecmd2pdf(1)](https://kristaps.bsd.lv/divecmd/divecmd2pdf.1.html),
which manages a *groff(1)* toolchain; and
[divecmd2divecmd(1)](https://kristaps.bsd.lv/divecmd/divecmd2divecmd.1.html),
which converts some dive computers' output into a form allowing for
better free dive analysis.

I forked [libdivecomputer](http://www.libdivecomputer.org)'s *dctool* to
write [divecmd(1)](https://kristaps.bsd.lv/divecmd/divecmd.1.html) ---
originally to provide output like
[Subsurface](https://subsurface-divelog.org/)'s XML format, but later to
significantly simplify the code, to add sandboxing via
[pledge(2)](http://man.openbsd.org/pledge.2), and to provide multiple
front-ends.

If you have a feature in mind, I'm very happy to be encouraged to
implement it with new dive computers, gear, or a place to dive...

See "[Dive computer
analysis](https://divelog.blue/cgi-bin/dblg/public.html?entryid=68)" and "[Visualising
recovery time when free
diving](https://divelog.blue/cgi-bin/dblg/public.html?entryid=69)" for examples of how
[divecmd2grap(1)](https://kristaps.bsd.lv/divecmd/divecmd2grap.1.html),
operates.

Also see "[Suunto D6i temperature
rounding](https://divelog.blue/cgi-bin/dblg/public.html?entryid=77)",
which showcases both temperature and split (by diver) modes.

## License

The sources use the LGPL as printed in the [LICENSE.md](LICENSE.md)
file.
Why not the usual ISC license?
Because this tool began as a fork of LGPL'd software.
