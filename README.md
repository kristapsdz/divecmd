## Synopsis

*divecmd* drives [libdivecomputer](http://www.libdivecomputer.org) to
extract the contents of your dive computer in a safe, simple manner.  It
has several programs: 

- [dcmd(1)](https://kristaps.bsd.lv/divecmd/dcmd.1.html), which
  downloads, parses, and formats dives;
- [dcmdterm(1)](https://kristaps.bsd.lv/divecmd/dcmdterm.1.html),
  which graphs formatted dives in an ANSI/VT100 terminal;
- [dcmd2grap(1)](https://kristaps.bsd.lv/divecmd/dcmd2grap.1.html),
  which graphs using *grap(1)*; 
- [dcmd2json(1)](https://kristaps.bsd.lv/divecmd/dcmd2json.1.html),
  which exports dive profiles to the JSON format;
- [dcmd2csv(1)](https://kristaps.bsd.lv/divecmd/dcmd2csv.1.html),
  which exports to CSV (usually for [Subsurface](https://subsurface-divelog.org/)).
- [dcmdfind(1)](https://kristaps.bsd.lv/divecmd/dcmdfind.1.html),
  which searches for dives;
- [dcmdedit(1)](https://kristaps.bsd.lv/divecmd/dcmdedit.1.html),
  which edits dive properties;
- [dcmdls(1)](https://kristaps.bsd.lv/divecmd/dcmdls.1.html),
  which lists dive profiles.

For interoperability with [Subsurface](https://subsurface-divelog.org/),
it features:

- [dcmd2ssrf(1)](https://kristaps.bsd.lv/divecmd/dcmd2ssrf.1.html),
  for convertint into Subsurface; and
- [ssrf2dcmd(1)](https://kristaps.bsd.lv/divecmd/ssrf2dcmd.1.html)
  for converting out.

It compiles and runs on Linux, Mac OS X, and OpenBSD.
For Mac OS X machines with homebrew, see BSD.lv's
[homebrew-repo](https://github.com/kristapsdz/homebrew-repo).

There are also some supporting utilities:

- [dcmd2pdf(1)](https://kristaps.bsd.lv/divecmd/dcmd2pdf.1.html), which
  manages a *groff(1)* toolchain into PDF; 
- [dcmd2ps(1)](https://kristaps.bsd.lv/divecmd/dcmd2ps.1.html), which
  does the same but for PS (Mac OS X's *groff(1)* doesn't have PDF support);
  and

I forked [libdivecomputer](http://www.libdivecomputer.org)'s *dctool* to
write [dcmd(1)](https://kristaps.bsd.lv/divecmd/dcmd.1.html) ---
originally to provide output like
[Subsurface](https://subsurface-divelog.org/)'s XML format, but later to
significantly simplify the code, to add sandboxing via
[pledge(2)](https://man.openbsd.org/pledge.2), and to provide multiple
front-ends.

If you have a feature in mind, I'm very happy to be encouraged to
implement it with new dive computers, gear, or a place to dive...

## Installation

To install *divecmd*, you need a recent version of
[libdivecomputer](http://www.libdivecomputer.org) and
[libexpat](http://expat.sourceforge.net/) (the latter is installed by
default on most modern UNIX machines).  You'll probably also want
*groff(1)* and *grap(1)*.  **Note**: if you're running Mac OS X, your
version of *groff(1)* doesn't support PDF output.

Just run the following:

```sh
./configure
make
sudo make install
```

Or if you're on OpenBSD, use `doas` instead of `sudo`.  That's it!

You'll probably need to provide paths to the third-party libdivecomputer
installation, however.
To do so, just add a file `configure.local` that includes the `LDFLAGS`
and `CPPFLAGS` set to the appropriate paths.
It will be automatically picked up when you run `configure`.
Alternatively, you can pas the `LDFLAGS` and `CPPFLAGS` variables to the
`configure` script.

**Note**: this is a shell script, not a Makefile component, so no spaces
around the equal sign.

## License

The sources use the LGPL as printed in the [LICENSE.md](LICENSE.md)
file.
Why not the usual ISC license?
Because this tool began as a fork of LGPL'd software.
