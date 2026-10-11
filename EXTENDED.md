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
- **Chart tools:** the TOOLS button of the chart's MAP CONTROLS opens a
  toolbox to draw on the map (pencil, lines labelled with range and bearing,
  range circles, marks, notes, a ruler, an eraser and undo), and the
  drawings follow the map at every zoom and are kept with saved missions.
- **Chart scrolling and zoom:** drag the chart with the mouse or a finger
  to scroll it, and zoom with the mouse wheel about the pointer.
- **Nautical chart scale:** the chart's scale bar reads in nautical miles,
  and in yards close in.
- **Boat choice:** the encounters' SUB CLASS screen starts on the class you
  chose last, if it is in service at the date, even after a restart.
- **Extended options:** the main menu's OPTIONS page, and the EXTENDED
  button of the in-mission GAME OPTIONS (Alt-O), switch the interface
  improvements on and off at once.
- **Turning the views:** drag the bridge, periscope, TBT or deck gun view to
  turn it, and a quick flick keeps it turning for a moment.
- **Precise dials:** drag a dial sideways to set its value, shown by an amber
  ghost needle and given when you let go.
- **Live ORDERS panel:** the touch ORDERS panel shows the depth order in
  effect and each tube's state.
- **Torpedo types:** each torpedo type has its own picture, the Mark 18-1
  blue and the 18-2 orange; the status page shows the types you sailed with,
  and the touch ORDERS panel names each tube's type.

## Rules

- **Sonar reports:** a ship the sound man has already reported is reported
  again, and the time compression reset, only after 3 minutes unheard.
- **Undeafened escorts:** a new REALISM item that gives the escorts the
  original's sharp sonar, which their own depth charges do not deafen, for
  a 10 % higher realism rating.
- **Detectable radar:** a new REALISM item, on at EXPERT, that gives the
  escorts radar detectors; turning it off lowers the realism rating by 5 %.
- **Mixed Mark 18s:** from August 1944 the Mark 18-2 replaces the Mark 18-1
  over three months, and a boat carries at most three torpedo types, its
  oldest type giving way to its newest.
- **Mark 27 "Cutie":** from mid-1944 boats can carry two homing torpedoes,
  fired on a sonar bearing, that home on the loudest screws ahead after 400
  yards, your own boat included after two minutes.

## Fixes of the original's bugs

- **Mark 14 faults:** with dud torpedoes on, each fault lasts until its
  historical fix date, not a month less.
- **Escort sonar:** depth-charge blasts deafen the escorts for a while, and
  a noise spoils their contact only when it is nearer, in the same
  direction.
- **Fog days:** once the fog lifts around midday, it stays clear until it
  is due back in the evening.
- **Enemy submarines:** in shallow water they stay above the bottom
  instead of diving into it.
- **Anti-aircraft guns:** they aim at the right elevation whatever the
  aircraft's direction, and no longer fire at targets below them.
- **Strafing aircraft:** they open fire only when lined up on your boat.
- **Ship routes:** a ship whose route starts in water too shallow for it
  drops that point, not the good one after it.
- **Ships aground:** a scenario ship is left out when its water is too
  shallow for it (none of the shipped scenarios has one).
- **Harbour defenders:** their crews get better as the war goes on, like
  everyone else's.
- **Hunter groups:** the patrols sent along an alerted convoy route have
  the size the zone's tables give, from 1 to 6 ships.
- **Unit awards:** a patrol counts toward the DSM and the unit citations
  with 2 ships sunk at 100 % realism or more, 3 below, 4 below 34 %.
- **Low on fuel:** a boat with less fuel than the trip home is interned
  near an enemy base only when nearly empty; otherwise it is towed home.
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
  warning of the original's 16 MB machine are gone; your scenarios still
  load in the original game.
- **Ship type:** cancelling SELECT SHIP TYPE no longer marks the scenario
  as changed.
- **City target:** cancelling SELECT A CITY from a mission's Location
  button keeps the mission and its target instead of deleting it.
- **Ship or submarine:** changing a vessel between ship and submarine classes
  keeps its course, and its orders when the new class can take them.
- **Hunter submarines:** enemy submarines can be ordered to Intercept a point
  clicked on the chart, or the player's position.
- **Routed ships:** every ship can be given Follow Path orders with a plotted
  course.
- **Boat names by date:** with "Limit by date: Subs", SELECT SUB NAME lists
  only the boats commissioned by the scenario's date.
- **Radar detector:** EQUIPMENT has a Radar Detector row, limited by date
  like the rest.
- **Hold time:** the airstrike's time over the target is labelled "Hold
  Time" instead of "Flight Delay".
- **Options file:** the editor says so when it cannot save its OPTIONS.
- **Reloads:** torpedo reloads from scenarios written by other tools go to
  the right torpedo type.
- **Torpedo dates:** with "Limit by date: Equipment", the Mark 18 torpedo
  is available from October 1943, its date in the game, instead of November.
- **Torpedo choices:** TORPEDOES offers the Mark 18-2 and the Mark 27, and
  explains that a boat carries at most three torpedo types.
