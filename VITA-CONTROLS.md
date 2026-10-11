# ZDoom Vita controls

See README.txt for the official user readme. Built-in defaults are used when
zdoom.ini is absent; saved bindings override them.

| Control | Gameplay | Menus |
| --- | --- | --- |
| Left stick | Move and strafe | Navigate |
| Right stick | Turn and look | — |
| Cross | Use/open | Confirm |
| Circle | Alternate attack | Back |
| Square | Jump (where allowed) | Clear binding |
| Triangle | Next weapon | — |
| L | Previous weapon | Previous page |
| R | Fire | Next page |
| Select | Toggle automap | — |
| Start | Open menu | Back |
| D-pad up | Use inventory item | Up |
| D-pad down | Center view | Down |
| D-pad left/right | Previous/next inventory item | Left/right |

## Launcher

D-pad browses; Left/Right or L/R switch IWAD and mod tabs. Cross selects;
Triangle clears mods; Square/Start launches. Circle returns to IWADs or exits.
Front touch selects files and launches. Archive selection order determines load
order; deselect/reselect to move an entry to the end. DeHackEd/BEX patches use
the engine's existing patch-loading phase. launcher.cfg remembers choices.

Put games in ux0:data/zdoom-vita/iwad/ and mods/patches in
ux0:data/zdoom-vita/pwad/. Subfolders are supported. Bundled shareware and
Freedoom games are also available. DOOM1.WAD is hidden when DOOM.WAD is present.
Place VOICES.WAD beside STRIFE1.WAD for automatic loading.
Selecting HEXDD.WAD also loads HEXEN.WAD automatically; keep both in iwad/
or its subfolders. Hexen remains available as a separate launcher choice.

## Touch, console, and settings

Front touch moves the menu cursor; a short stationary tap activates. Holding or
dragging and then lifting does not click. Rear-right selects the next weapon and
rear-left selects the previous weapon during gameplay. Vita Settings can disable
rear switching and change render resolution or FPS display. Default scaling is
2x; the FPS counter defaults off.

Go to Console opens the Vita keyboard. Enter submits; Cancel leaves the console
open. Cross or a front-screen tap opens another dialog; Circle/Start exits.
vita_inventory_defaults restores D-pad Left/Right/Up inventory bindings and
clears their double-press bindings.

Configuration is saved in ux0:data/zdoom-vita/zdoom.ini. Diagnostic logs are
boot-trace.log, startup.log, and startup-errors.log in the same base folder.
