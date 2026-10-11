# Equipment dates

The dates from which the game lets a submarine have each piece of equipment.
These are the dates the game itself uses. Where the manual says otherwise,
it is noted.

Dates are month and year; the day is always the 1st. One table of SH.EXE
(0x10e408) gives the radar, PPI, radar detector and bathythermograph dates
to the single missions, the careers and the scenario editor. The editor's
copy (SE.EXE 0x9ea30) is the same, byte for byte.

## Radars and sensors

- **SD air-search radar:** January 1942.
- **SJ surface radar (SJa):** August 1942.
- **Improved SJ radar (SJ-1):** August 1943. It replaces the SJa. The
  manual's weapons table says December 1942, which the Commander's Edition
  errata corrects to August 1943.
- **A-scope:** no date of its own. It is the radar display a boat with a
  surface radar always has. The manual dates it December 1941.
- **PPI scope:** July 1943. The manual says September 1943.
- **Radar detector (APR-1):** May 1944. The original game only stores this
  equipment bit and never uses it. Careers never get it.
- **Bathythermograph:** March 1943.

## Torpedoes

- **Mark 10:** always available. It is the only torpedo of the classes
  without modern tubes (the S-boats).
- **Mark 14:** always available to the modern classes, and their default.
- **Mark 18-1:** October 1943.
  - In single missions, from then on a growing share of the load: 20 % in
    1943, 40 % in 1944, 60 % in 1945.
- **Mark 18-2:** August 1944.
  - The original's single missions never load it: they look up the
    Mark 18-1 again.

## Careers

- **Delivery:** a career boat gets each radar, the PPI and the
  bathythermograph at its date in a home port of ComSubPac. In the other
  commands these items come 3 months later, at a refit.
- **Torpedoes:** the Mark 18-1 and Mark 18-2 are base supplies, available
  from their dates without delay.
- **The radar detector:** never delivered.

## Scenario editor

- **Fits:** with "Limit by date: Equipment", the EQUIPMENT dialog allows
  each fit from the dates above.
- **Torpedoes:** with "Limit by date", the original editor's TORPEDOES
  dialog allows each torpedo a month late. It compares the 1-based month of
  WEAPONS.DAT with the scenario's 0-based month, so the Mark 18-1 comes in
  November 1943 and the Mark 18-2 in September 1944. The extended build's
  editor uses the right months.

## Guns

- **Deck and anti-aircraft guns** come with the boat's class (SUBS.DAT),
  not by date. The manual dates them in its gun table:
  - 20 mm: November 1942.
  - 40 mm: April 1944.
  - 3 in/50: 1938.
  - 5 in/51: July 1942.
  - 5 in/25: August 1944.
