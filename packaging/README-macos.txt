@NAME@ @VERSION@
==============================

An open-source reimplementation of Silent Hunter (MicroProse, 1995). It is
only an engine: you need your own copy of the original game.

https://github.com/conning-tower/SilentHeritage

Installing
----------

Drag @NAME@ onto the Applications folder. It runs on macOS 13.3 or later,
on Apple Silicon and Intel Macs.

The app is not notarized by Apple: the first time, macOS refuses to open
it ("Apple could not verify..."). Click Done, then open System Settings,
Privacy & Security: under Security, click "Open Anyway" next to
@NAME@, and confirm. macOS remembers it for the next times.

Or, from the Terminal:

    xattr -dr com.apple.quarantine "/Applications/@NAME@.app"

Running
-------

On the first start, the game asks for the folder of your Silent Hunter
install (the one holding SH.EXE and DATA) and remembers it. Saves go to
~/Library/Application Support/Conning Tower/@NAME@/, never into the
install. The project's page describes the command-line options:

    "/Applications/@NAME@.app/Contents/MacOS/@EXECUTABLE@" --setup

Licences
--------

Silent Heritage: MIT licence. SDL: zlib licence. Both in the app:
@NAME@.app/Contents/Resources/licenses.

Silent Heritage is not affiliated with or endorsed by MicroProse, Ubisoft,
or any owner of the Silent Hunter trademark or of the original game.
