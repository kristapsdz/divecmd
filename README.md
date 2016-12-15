## Synopsis

**divecmd** drives [libdivecomputer](http://www.libdivecomputer.org) to
extract the contents of your dive computer in a safe, simple manner.

I forked the **dctool** to do so, originally to provide output like
[Subsurface](https://subsurface-divelog.org/)'s XML format, but later
to significantly simplify the code, to add sandboxing via
[pledge(2)](http://man.openbsd.org/pledge.2), and to provide more
auto-detection support (forthcoming).

## License

The sources use the LGPL as printed in the [LICENSE.md](LICENSE.md)
file.
Why not the usual ISC license?
Because this tool began as a fork of LGPL'd software.
