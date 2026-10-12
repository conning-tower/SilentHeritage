@NAME@ @VERSION@
==============================

An open-source reimplementation of Silent Hunter (MicroProse, 1995). It is
only an engine: you need your own copy of the original game.

https://github.com/conning-tower/SilentHeritage

Running
-------

Unzip the folder anywhere and start @EXECUTABLE@.exe.

On the first start, the game asks for the folder of your Silent Hunter
install (the one holding SH.EXE and DATA) and remembers it; `--setup` asks
again, `--data <folder>` gives it on the command line. Saves go to
%APPDATA%\Conning Tower\@NAME@\, never into the install. The project's
page describes the other options.

The program is not signed yet: Windows SmartScreen may show "Windows
protected your PC" on its first start. Click "More info", then "Run anyway".
You can check that the file is the one built by the project's release
workflow: see the release's page on GitHub.

Licences
--------

Silent Heritage: MIT licence (licenses\SilentHeritage.txt). SDL: zlib
licence (licenses\SDL3.txt).

Silent Heritage is not affiliated with or endorsed by MicroProse, Ubisoft,
or any owner of the Silent Hunter trademark or of the original game.
