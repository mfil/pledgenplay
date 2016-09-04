Pledge 'n' Play
===============

Pledge 'n' Play is a paranoid music player/decoder. Decoding of music
files happens in a child process that is locked down using OpenBSD's
`pledge` system call to limit the system calls it can use.

Malicious audio files are probably not something that anyone is really
worried about -- if you are, tell me what browser you're using. I'm
writing this to get some practice with privilege separation and unit
testing. Nevertheless, I hope to make this a functional music player
some day. So far, it can decode the FLAC format to WAVE and raw audio
data.
