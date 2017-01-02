## Synopsis

*divecmd* drives [libdivecomputer](http://www.libdivecomputer.org) to
extract the contents of your dive computer in a safe, simple manner.

I forked [libdivecomputer](http://www.libdivecomputer.org)'s *dctool* to
do so, originally to provide output like
[Subsurface](https://subsurface-divelog.org/)'s XML format, but later to
significantly simplify the code, to add sandboxing via
[pledge(2)](http://man.openbsd.org/pledge.2), and to provide more
auto-detection support (forthcoming).

**It is still in the earliest phases, and is tailored mostly to my our
computers, a [Suunto
D6i](http://www.suunto.com/en-US/Products/Dive-Computers-and-Instruments/Suunto-D6i-Novo/Suunto-D6i-Novo-Black/)
and a [Heinrichs Weikamp OSTC
2](http://heinrichsweikamp.com/ostc2.html).**

I welcome being sent on dives with another dive computer for better
support...

## License

The sources use the LGPL as printed in the [LICENSE.md](LICENSE.md)
file.
Why not the usual ISC license?
Because this tool began as a fork of LGPL'd software.
