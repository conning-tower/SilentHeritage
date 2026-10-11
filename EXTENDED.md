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
  chose last, when it is in service at the date. It is remembered between
  sessions.
- **Extended options:** the main menu's OPTIONS page switches the interface
  improvements on and off. During a mission, the EXTENDED button of GAME
  OPTIONS (Alt-O) does the same, at once.
- **Turning the views:** drag the view of the bridge, periscope, TBT or deck
  gun to turn it; the sea follows the pointer or finger, and a quick flick
  keeps it turning for a moment. Holding the arrows still works.
- **Precise dials:** press a dial (ordered course, rudder, depth, the TDC's
  knobs and counters) and drag sideways. An amber ghost needle and a readout
  show the new value, given when you let go. Drag farther above or below
  the dial, or hold Shift, for finer steps; the right button or Esc
  cancels; the mouse wheel steps the value. A plain click works as before.
- **Live ORDERS panel:** on touch screens, the ORDERS panel lights the depth
  order in effect and shows each tube as loaded, reloading (with the time
  left), empty or damaged.

## Rules

- **Sonar reports:** a ship the sound man has already reported is reported
  again only after 3 minutes unheard. Escorts fading in and out no longer
  reset the time compression every minute while you are hunted.
- **Keen escort sonar:** a new REALISM check box. Unchecked, the escorts'
  sonar is deafened for a while by their own depth charges, and a nearby
  noise only spoils a contact when it comes from the same direction and is
  nearer. Checked, the escorts keep the original's sharper ears, and the
  realism rating is 10 % higher (110 % at Expert). It works in single
  missions and careers alike.

## Fixes of the original's bugs

- **Mark 14 faults:** with dud torpedoes on, each fault lasts until its
  historical fix date, not a month less.
- **Fog days:** once the fog lifts around midday, it stays clear until it
  is due back in the evening.
- **Enemy submarines:** in shallow water they stay above the bottom instead
  of diving into it.
- **Anti-aircraft guns:** they aim at the right elevation whatever the
  aircraft's direction, and no longer fire at targets below them.
- **Strafing aircraft:** they open fire only when lined up on your boat.
- **Ship routes:** a ship whose route starts in water too shallow for it
  drops that point, instead of the good one after it.
- **Harbour defenders:** their crews get better as the war goes on, like
  everyone else's.
- **Hunter groups:** the patrols sent along an alerted convoy route have
  the size the zone's tables give, from 1 to 6 ships.
- **Patrol start:** a career patrol starts only from the map edges the zone
  allows.
- **New careers:** a new career's equipment no longer depends on the last
  letter you read.
- **New boats:** the boat you are given was in service on the date, and a
  repeat draw stays in the same class instead of putting you in an S-boat.
- **Encounter dates:** a single mission's random day always exists in its
  month (no more 31 February).
- **Recognition manual:** clicking a tab on the closed book opens it
  properly.

## Scenario editor

- **No size limit:** the memory gauge and the "SCENARIO IS TOO LARGE"
  warning of the original's 16 MB machine are gone. Your scenarios still
  load in the original game.
- **Ship type:** cancelling SELECT SHIP TYPE no longer marks the scenario as
  changed.
