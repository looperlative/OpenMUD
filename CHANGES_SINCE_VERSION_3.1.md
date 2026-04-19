# Summary of Changes Since `f82b7751`

64 commits, +5,546 / -986 lines across 69 files.

## OLC / Editing System (largest chunk)
- Replaced incomplete OLC with custom system (`src/olc.c`, `src/olc.h`)
- **REDIT** (rooms), **OEDIT** (objects), **MEDIT** (mobs) editors
- `roommerge`, `mobmerge`, `oedit→obj` merge scripts (`bin/roommerge.pl`, etc.)
- Saves to `redit.wld`, `oedit.obj`, `medit.mob`; merged back into world files
- REDIT/MEDIT/OEDIT now create new prototypes on-the-fly when a vnum within a valid zone range doesn't exist yet (`olc_create_room_proto`, `olc_create_mob_proto`, `olc_create_obj_proto`)
- Validation changed: first checks vnum is within a zone range, then creates prototype if needed

## ZEDIT — Zone Management
- **Permission subcommands** (GRGOD+): `create`, `open`, `close`, `grant author|editor`, `revoke author|editor`
- **Info subcommand**: `info` for any zone author/editor to view flags, authors, editors, lock state
- **Lock/unlock lifecycle**: `lock` extracts all mobs/objects and resets the zone; `unlock` writes `.zon` file (with `.bak`) and releases lock; only the lock-holder or GRGOD+ may unlock
- **Reset-command editing** (locked zone only): `mobile`, `object`, `give`, `equip`, `put`, `door`, `list`, `remove` subcommands; each mutation re-runs `clean_zone` + `reset_zone` for immediate feedback
- `zedit_save_zone_file` translates rnums back to vnums on save so `.zon` files round-trip correctly

## In-Game Help
- Added `wizhelp.hlp` entries for **OLC**, **MEDIT**, **OEDIT**, **REDIT**, and **ZEDIT**

## Gameplay / Balance
- Mage spell damage & mana rework; new spells: **memorize**, **teleport to**, **remove curse** scroll
- Thief **EVADE TRAP** skill; warrior/thief **double/triple attack**
- **AVOID** (renamed from EVADE); **auto-assist**; **autoeq** now default
- Regen rate adjustments; tick time shortened to 60s then restored to 75
- Rebirth/ascension: Room of Introspection, reset age/practices/exp, preserve natural stats
- Rolling system revised; generous rolls with reroll-on-minimum
- Movement point usage toggle; sanc/heal mana reduced
- Survey weapons for damage range; `memories` command; `vlist` command; improved practice

## Zone Reset
- Performance improvements; track specific mob/obj per rule
- **ZCLEAN** + auto zone cleanup after 5 unplayed reset cycles
- Bug fixes in Zone 25 (wrong chest key, missing long descs)

## Infrastructure / Ops
- **HAPROXY** protocol support
- Separate `lib/` (distribution) from `lib-run/` (runtime)
- `permission.dat` binary + code support
- Autosave player file on reboot
- Mudwho HTML exporter, `who` HTML export, `mudwho.css`
- `bootstrap.sh`, `libdiff.sh`; `.gitignore` cleanup; `cmphtml.pl` fix
- Removed obsolete platform support (Amiga `autorun.amiga`, Windows `autorun.cmd`, VMS `vms_autorun.com`, Mac `macrun.pl`)
- Cleared all gcc/ubuntu warnings

## UI / Quality of Life
- Opponent + tank status in prompt
- Improved auto-exits (immortals see more)
- Players can view spell affects and natural abilities; stats visible at level 10+
- PRF flag strings updated; autoassist SYSERR fixed
- **Equipment sets** (`eqset` command): players can save up to 5 named loadouts and switch between them; items move between worn positions and inventory without being created or destroyed; missing items reported on load; sets persist across sessions in `lib/plreqsets/`
