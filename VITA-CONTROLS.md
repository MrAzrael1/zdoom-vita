# ZDoom Vita controls

See README.txt for the official user readme. Built-in defaults are used when
zdoom.ini is absent; saved bindings override them.

| Control | Gameplay | Menus |
| --- | --- | --- |
| Left stick | Move and strafe | Navigate |
| Right stick | Turn and look | — |
| X | Use/open | Confirm |
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

D-pad browses; Left/Right or L/R switch IWAD, Mods and Configs tabs.
X selects files or opens a folder. Hold X for about half a second on
a folder to toggle loading the entire folder through -file. Circle goes up a
folder, cancels an active config on Configs, or returns to IWADs. At IWADs,
Circle exits. Start or the touchscreen Start Game button launches.
Triangle clears mods; Square deletes the highlighted config on Configs.
Select opens the native launch-argument editor.

Configs save the IWAD and ordered selected mods/folders. X on a saved
config loads it; X on it again disables it, clearing mods while keeping
the chosen IWAD. Every launcher session starts with no selected mods, folders, active config or
extra arguments. The last IWAD is retained; explicitly load a saved config
to restore its IWAD and mod list.
Config files are in ux0:data/zdoom-vita/configs/. Saving an existing name replaces
its IWAD and full mod list. Older modlist-only files remain
readable and leave the IWAD unchanged. Each config allows eight archives/folders
and one DEH/BEX patch entry; folder contents are handled by the engine.

Front touch selects files/folders and launches. Selection order determines load
order; deselect/reselect to move an entry to the end. DeHackEd/BEX patches use
the engine's existing patch-loading phase. launcher.cfg retains the last IWAD. Transient choices are reset on startup.

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
open. X or a front-screen tap opens another dialog; Circle/Start exits.
vita_inventory_defaults restores D-pad Left/Right/Up inventory bindings and
clears their double-press bindings.

Configuration is saved in ux0:data/zdoom-vita/zdoom.ini. Diagnostic logs are
boot-trace.log, startup.log, and startup-errors.log in the same base folder.

Preload effects (Vita Settings) moves some effect sprite and sound decoding to
map loading to reduce first-use hitches. It defaults On and takes effect when
loading a map; the engine's precache setting must also be enabled. Loading may
take longer. Supplemental budgets are 32MiB of sprite pixels and 16MiB of PCM;
remaining assets still load normally when used. It does not lower effect quality.

### Launcher Options (v0.12.0 pass17)

The fourth tab, Options, contains Verbose Logging: On/Off. Press X or tap the row to toggle it. L/R cycles through all four tabs. The default is Off; the choice is saved in launcher.cfg immediately and retained between launches. It controls vita_profile performance instrumentation/reports. Startup, errors and occasional configuration-save logs remain available. Enabling it may reduce performance; use it when collecting a bug report.

### Startup display (v0.12.0 pass18)

After Start Launch, ZDoom startup messages appear in a scrolling text display until engine graphics take over. This works with Verbose Logging off. Entering launch arguments preserves the launcher folder header.

### Original startup styles (v0.12.0 pass19 experiment)

Startup honors the selected game/mod startup style, including Heretic text, Hexen artwork/music/ticks and Strife animation. Ordinary games use the VGA-font console with progress. Use -nostartup to skip special graphical styles. Verbose Logging is independent. Test startup sound continuity and gameplay handoff; pass18 remains the earlier scrolling-console fallback build.

### Classic Doom console cheats (v0.12.0 pass21)

Use Go to Console and the Vita keyboard. Existing iddqd, idclev and idmus remain unchanged. Example: idclev 07 selects MAP07 in Doom II; idclev 21 selects E2M1 in Doom. idmus 7 selects the corresponding music.

New commands: idkfa (weapons/ammo/armor/keys/backpack), idfa (same without keys), idclip/idspispopd (noclip), idchoppers (chainsaw), idbeholdv (invulnerability), idbeholds (berserk), idbeholdi (invisibility), idbeholdr (radiation protection), idbeholda (map reveal), idbeholdl (light amplification). idbehold lists the choices. iddt cycles normal/all-lines/all-lines-and-things automap modes, usable while the console is open. Existing idmypos true/false or toggle idmypos controls position display. Cheat permissions are retained; new gameplay codes require a running level.

### Folder semantics and console width (v0.12.0 pass22)

Holding X on a folder passes it directly to ZDoom as -file, preserving unpacked-PK3 behavior. Loose resources are scanned recursively. WADs at the selected folder root are opened as embedded archives in engine filename order. Arbitrary nested WADs are not all automatically opened; the engine also recognizes its existing archive-named-folder special case. PK3/ZIP archives inside the selected folder are not opened automatically in 2.8.1: select those separately in Mods. A folder is not an automatic batch modlist; use Configs for explicit ordering. No recursive PK3 autoload was added.

The basic VGA startup console now uses the full 960x544 screen; special Heretic/Hexen/Strife startup artwork retains its previous proportions.
