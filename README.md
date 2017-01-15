## Synopsis

*divecmd* drives [libdivecomputer](http://www.libdivecomputer.org) to
extract the contents of your dive computer in a safe, simple manner.  It
has three programs: *divecmd*, which downloads, parses, and formats dives;
*divecmd2term*, which graphs formatted dives in an ANSI/VT100 terminal;
and *divecmd2grap*, which graphs using *grap(1)*.

I forked [libdivecomputer](http://www.libdivecomputer.org)'s *dctool* to
write *divecmd* --- originally to provide output like
[Subsurface](https://subsurface-divelog.org/)'s XML format, but later to
significantly simplify the code, to add sandboxing via
[pledge(2)](http://man.openbsd.org/pledge.2), and to provide multiple
front-ends.

**It is still in the earliest phases of development.** Though I welcome being
sent on dives to give the system more support...

## License

The sources use the LGPL as printed in the [LICENSE.md](LICENSE.md)
file.
Why not the usual ISC license?
Because this tool began as a fork of LGPL'd software.
