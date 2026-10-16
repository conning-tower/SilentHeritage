@NAME@ @VERSION@
==============================

An open-source reimplementation of Silent Hunter (SSI, 1996). It is
only an engine: you need your own copy of the original game.

https://github.com/conning-tower/SilentHeritage

Running
-------

    ./@EXECUTABLE@

On the first start, the game asks for the folder of your Silent Hunter
install (the one holding SH.EXE and DATA) and remembers it; `--setup` asks
again, `--data <folder>` gives it on the command line. Saves go to
~/.local/share/Conning Tower/@NAME@/, never into the install. BUILD.md, on the
project's page, describes the other options.

The program needs only the system's C library (glibc 2.35 or later, as in
Ubuntu 22.04 and Debian 12): SDL, linked in, uses the X11 or Wayland
display and the PulseAudio, PipeWire or ALSA sound of the system.

Menu entry
----------

To add the game to your desktop's menu, put the program in a folder of
your PATH and the .desktop file and the icon where your desktop finds them:

    install -m 755 @EXECUTABLE@ ~/.local/bin/
    install -m 644 *.desktop ~/.local/share/applications/
    install -m 644 *.png ~/.local/share/icons/hicolor/256x256/apps/

(The AppImage of the same release needs none of this.)

Licences
--------

Silent Heritage: MIT licence (licenses/SilentHeritage.txt). SDL: zlib
licence (licenses/SDL3.txt).

Silent Heritage is not affiliated with or endorsed by SSI, Ubisoft,
or any owner of the Silent Hunter trademark or of the original game.
