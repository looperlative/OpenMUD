# OLC System Evaluation & Completion Plan

## Context

An in-MUD online area-editing system (OLC) covers mobiles, objects, rooms, and zones, backed by a permission system (`permission.dat`) that gates editing by zone author/editor assignments. All four editors are now functional for day-to-day area building; zedit can place mobs/objects into rooms and edit doors via reset-command editing.

All editor logic lives in `src/olc.c` with headers in `src/olc.h`. The generic state machine (`olc_nanny`) handles text edit, toggle-bit, type-select, numeric, extra-desc, and exit-edit states, with a two-slot state stack for nested sub-editors. Merged output is appended to editor-specific files (`medit.mob`, `oedit.obj`, `redit.wld`) and later reconciled by the existing `mobmerge` / `roommerge` tools.

## Current State by Editor

### MEDIT — ~95% complete
Covers all core mob stats: name, short/long/detail desc, action flags, affects, alignment, level, AC, hitroll, HP/damage dice, gold, exp, position, sex, attack type, abilities. Save writes `medit.mob`. Permissions enforced via `olc_ok_to_edit()`. Creates a new mob prototype (`olc_create_mob_proto`) if the vnum is within a valid zone range but doesn't exist yet.
**Missing:** extra descriptions, special-procedure/script assignment, default inventory/equipment.

### OEDIT — ~90% complete
Aliases, descriptions, type, wear/extra flags, weight/cost/rent, type-specific values (weapon/armor/potion/staff/container), affects, extra descs. Save writes `oedit.obj`. Permissions enforced via `olc_ok_to_edit()`. Creates a new object prototype (`olc_create_obj_proto`) if the vnum is within a valid zone range but doesn't exist yet.
**Missing:** quest-item flags, spell/charge editing polish on magical types.

### REDIT — ~90% complete
Name, desc, flags, sector, six exits with key/lock data, extra descs. Save writes `redit.wld`. Permissions enforced via `olc_ok_to_edit()`. Creates a new room prototype (`olc_create_room_proto`) if the vnum is within a valid zone range but doesn't exist yet.
**Missing:** nothing structural — mob/object placement is properly the zedit's job.

### ZEDIT — ~90% complete
Zone permission management, the lock/unlock lifecycle, and reset-command editing are all functional. Subcommands:

| Command | Permission | Description |
|---------|-----------|-------------|
| `zedit <zone> create <name>` | GRGOD+ | Create a new zone |
| `zedit <zone> info` | Author/Editor | View zone flags, authors, editors |
| `zedit <zone> open` | GRGOD+ | Open zone to mortals |
| `zedit <zone> close` | GRGOD+ | Close zone to mortals |
| `zedit <zone> lock` | Author/Editor | Extract + reset zone, hold at reset state, block auto-resets |
| `zedit <zone> unlock` | Lock holder or GRGOD+ | Save `.zon` (with `.bak`), release lock |
| `zedit <zone> grant author\|editor <player>` | GRGOD+ | Add player to author/editor list |
| `zedit <zone> revoke author\|editor <player>` | GRGOD+ | Remove player from author/editor list |
| `zedit <zone> list` | Author/Editor | List reset commands with human-readable descriptions |
| `zedit <zone> mobile <mob vnum> <room vnum>` | Lock holder (LOCKED) | Append `M` reset command |
| `zedit <zone> object <obj vnum> <room vnum>` | Lock holder (LOCKED) | Append `O` reset command |
| `zedit <zone> give <obj vnum>` | Lock holder (LOCKED) | Append `G` reset command (attaches to most recent M) |
| `zedit <zone> equip <obj vnum> <wear pos>` | Lock holder (LOCKED) | Append `E` reset command |
| `zedit <zone> put <obj vnum> <container vnum>` | Lock holder (LOCKED) | Append `P` reset command |
| `zedit <zone> door <room vnum> <dir 0-5> <state 0-2>` | Lock holder (LOCKED) | Append `D` reset command |
| `zedit <zone> remove <index>` | Lock holder (LOCKED) | Delete reset command at index |

**Lock semantics:** `lock` calls `clean_zone()` to extract all mobs/objects, then `reset_zone()` to spawn everything fresh from the reset command table. The auto-reset loop (`zone_update`, `db.c:2089`) already skips zones with nonzero `permissions.flags`, so LOCKED inherently blocks automatic resets.

**Unlock semantics:** `unlock` writes `lib-run/world/zon/<vnum>.zon` (renaming any existing file to `<vnum>.zon.bak` first) from in-memory `zone_table[rnum]` before releasing the lock. Only the player who locked the zone (or a grgod) may unlock it. Save converts rnums back to vnums per command type so the file round-trips correctly.

**Reset-command semantics:** Each successful mutation re-syncs the world (`clean_zone` + `reset_zone`) so the builder sees the effect immediately. `if_flag` and `max` defaults are fixed per command type (M/O: 0/1; G/E/P: 1/1; D: 0). Wear position, direction, and door state are validated against `NUM_WEARS` / `NUM_OF_DIRS` / `[0,2]`.

**Still missing:** insert-at-index (workaround: remove + re-add in order), PURGE (no runtime support), if-flag/max override (hand-edit `.zon` if needed), quest/trigger integration.

## Permission Model

- **GRGOD+** can: create zones, open/close zones, grant/revoke author/editor permissions, and bypass all permission checks.
- **Zone authors/editors** can: view zone info (`info`), lock/unlock zones, and use medit/oedit/redit on vnums within their zone (zone must be CLOSED).
- **Mortals** cannot enter CLOSED zones or rent items from them.
- Permissions are stored in `permission.dat` as binary records indexed by zone vnum.
- Helper functions: `olc_is_zone_author()`, `olc_is_zone_author_or_editor()`, `olc_ok_to_edit()`, `olc_ok_to_enter()`, `olc_ok_to_use_or_rent()`.

## Infrastructure Already in Place

- Generic menu/state machine with nested state stack (`olc_nanny`).
- Permission persistence via `olc_save_permissions` + `permission.dat` in both `lib/` and `lib-run/`.
- Zone reset semantics (M/O/G/E/P/D/S) already interpreted by the reset engine; `struct reset_com` is the storage target.
- Merge utilities (`mobmerge`, `roommerge`) for folding `*.mob` / `*.wld` edits back into canonical zone files.
- `get_ptable_by_name()` exported in `db.h` for player name resolution.
- zedit accessible at `LVL_IMMORT` in interpreter; subcommands enforce their own permission levels.
- In-game help entries for OLC, MEDIT, OEDIT, REDIT, ZEDIT in `lib/text/help/wizhelp.hlp`.

## Work Remaining (priority order)

### 1. Small gaps in existing editors
- Medit: extra-description state (reuse REDIT's extra-desc handler), special-proc field.
- Oedit: quest flag, spell-charge validation.

### 2. Zedit polish
- Insert-at-index (`zedit <zone> insert <after-index> ...`) — currently only append + remove.
- If-flag / max override for reset commands — currently fixed per command type.

### 3. Quality-of-life
- In-progress edit recovery: periodic snapshot of the active `olc_data` to a scratch file keyed on player id, restored on reconnect.

## Critical Files

- `src/olc.c` — all editor logic and zedit subcommands.
- `src/olc.h` — state constants, `olc_permissions_s` struct, function declarations.
- `src/db.h` — `get_ptable_by_name()` declaration.
- `src/db.c` / zone loader — reference for zone-save output format.
- `lib-run/world/zon/*.zon` — reference zone file format.
- `src/interpreter.c` — zedit wired at `LVL_IMMORT`.
