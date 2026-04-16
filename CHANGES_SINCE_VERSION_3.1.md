# Summary of Changes Since `f82b7751`

Approximately 60 commits, +4,379 / -678 lines across 58 files.

## OLC / Editing System (largest chunk)
- Replaced incomplete OLC with custom system (`src/olc.c` +2,160 lines, `src/olc.h` +152)
- **REDIT** (rooms), **OEDIT** (objects), **MEDIT** (mobs) editors
- `roommerge`, `mobmerge`, `oedit→obj` merge scripts (`bin/roommerge.pl`, etc.)
- Saves to `redit.wld`, `oedit.obj`, `medit.mob`; merged back into world files
- Zone permissions + beginnings of zone editing

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
- Cleared all gcc/ubuntu warnings

## UI / Quality of Life
- Opponent + tank status in prompt
- Improved auto-exits (immortals see more)
- Players can view spell affects and natural abilities; stats visible at level 10+
- PRF flag strings updated; autoassist SYSERR fixed
