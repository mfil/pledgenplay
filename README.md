Pledge 'n' Play
===============

Pledge 'n' Play is a paranoid music player/decoder. Decoding of music
files happens in a child process that is locked down using OpenBSD's
`pledge` system call to limit the system calls it can use.

Malicious audio files are probably not the thing you're most worried
about. I'm writing this to get some practice with privilege separation
and unit testing. Nevertheless, I hope to make this a (more or less)
functional music player some day. So far, it can decode or play back
a single file in the FLAC format.

To run, [libflac](https://xiph.org/flac/) is required. To build the
unit tests, the [Check](https://libcheck.github.io/check/) framework
is required.
