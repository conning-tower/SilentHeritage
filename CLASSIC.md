# Silent Heritage: differences from the original game

Silent Heritage, the classic version, plays the original game: the
same simulation, rules, screens and limits, original bugs included. The
differences below make it run on modern machines and touch screens, or
avoid crashes and data loss. None of them changes the simulation. The
improvements of Silent Heritage Extended are in [EXTENDED.md](EXTENDED.md).

## Running on modern systems

- **Files:** the game reads your own Silent Hunter install, chosen once in a
  folder dialog, and never writes into it. Saves,
  options and custom scenarios go to your per-user folder.
- **Display:** the 640×480 picture is scaled to the window with smoothing,
  and the window keeps its 4:3 shape when resized. Alt+Enter toggles
  fullscreen, which keeps the mouse on the picture, off the black bars of a
  screen of another shape.
- **Mouse pointer:** it is drawn wherever the mouse is in the window,
  instead of stopping at the picture's edge. When the game moves it (the
  chart's edge scrolling), the mouse follows; where the system does not let
  it (some Linux desktops), the pointer stays with the mouse instead, and
  the chart keeps scrolling while it is on the edge.
- **Keyboard:** keys follow your keyboard layout (AZERTY, QWERTZ, BÉPO...),
  so each command is on the key that types its letter.
- **Chart:** in a window, edge scrolling reacts to a band a few pixels wide
  along the screen's edges; in fullscreen, where the mouse cannot leave the
  picture, it reacts only at the very edge.

## Android

- **Touch:** a finger acts as the left mouse button. A small floating ORDERS
  button opens the orders that are keyboard-only in the original (depth
  orders, firing tubes), and its − and + buttons change the time
  compression. The button can be dragged anywhere; it keeps its place
  relative to the screen when the window changes (folding or unfolding a
  phone, a resize), always stays inside it, and has its right size from the
  start.
- **Chart:** dragging the map with a finger scrolls it, and the pointer no
  longer scrolls it at the screen's edges.
- **Back:** goes back one screen in the menus and quits from the main menu. In a
  mission it goes back to the conn, where it opens the END MISSION menu, and
  otherwise acts as Esc (closing a menu or ending the chart's plotting), as in
  the scenario editor.
- **Text entry:** the system keyboard opens for names and descriptions.
- **Pointer:** the game's mouse pointer is not drawn, since your finger
  points.
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
