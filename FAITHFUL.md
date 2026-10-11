# Silent Heritage: differences from the original game

Silent Heritage (the default, faithful build) plays the original game: the
same simulation, rules, screens and limits, original bugs included. The
differences below make it run on modern machines and touch screens, or
avoid crashes and data loss. None of them changes the simulation. The
extended build's improvements are in [EXTENDED.md](EXTENDED.md).

## Running on modern systems

- **Files:** the game reads your own Silent Hunter install, chosen once in a
  folder dialog (`--setup` to change it), and never writes into it. Saves,
  options and custom scenarios go to your per-user folder.
- **Display:** the 640×480 picture is scaled to the window with smoothing
  (`--sharp` for the original's square pixels). Alt+Enter toggles
  fullscreen, which keeps the mouse inside the picture.
- **Keyboard:** keys follow your keyboard layout (AZERTY, QWERTZ, BÉPO...),
  so each command is on the key that types its letter.
- **Chart:** edge scrolling reacts to a wider band along the screen's edges.

## Android

- **Touch:** a finger acts as the left mouse button. A small floating ORDERS
  button opens the orders that are keyboard-only in the original (depth
  orders, firing tubes), and its − and + buttons change the time
  compression. The button can be dragged anywhere; it keeps its place
  relative to the screen when the window changes (folding or unfolding a
  phone, a resize), always stays inside it, and has its right size from the
  start; `--no-orders-button` hides it.
- **Back:** goes back one screen in the menus and quits from the main menu.
  During a mission and in the scenario editor it acts as Esc.
- **Text entry:** the system keyboard opens for names and descriptions.
- **Leaving the app:** the game is kept. The next start resumes where you
  were.
- **Sound:** when the system cuts the sound output (a call, the screen
  off, folding the phone, coming back to the app), the game reopens it
  instead of staying silent until restarted.

## Fixes

- **Scenario editor:** several of the original editor's bugs that lose data
  or corrupt scenarios are fixed. They are listed in
  [docs/editor-design.md](docs/editor-design.md), "Fixed in the
  reimplementation".
- **Interviews:** the frame slider under an interview video works: click or
  drag it to jump to any moment of the video. The original left it inert.
- **Hunter groups:** an anti-submarine patrol on an alerted convoy route is
  capped at 6 ships; with more, the original game hangs when it comes into
  sight.
