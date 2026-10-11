# Silent Heritage Extended: improvements and new features

The extended build (`-DSH_VARIANT=extended`) is the same game with
improvements beyond the original. It installs next to the faithful build,
keeps its own saves, and `--faithful` turns all its improvements off. It
also has every change of [FAITHFUL.md](FAITHFUL.md).

Interface improvements never change the simulation. Rule changes and fixes
of the original's bugs are set when a mission or a career starts, and are
kept with it.

## Interface

- **Crew log:** click the message strip at the bottom of the screen, or press
  L, to read the mission's last 100 crew messages with their game times.
- **Boat choice:** the encounters' SUB CLASS screen starts on the class you
  chose last, when it is in service at the date.

## Rules

- **Sonar reports:** a ship the sound man has already reported is reported
  again only after 3 minutes unheard. Escorts fading in and out no longer
  reset the time compression every minute while you are hunted.
