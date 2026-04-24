# Summary of Changes Since `f82b7751`

## Web-Based OLC (`src/webserver_olc.c`, `src/webserver_olc.h`, `lib/www/olc/`)
- Full web-based OLC matching all in-game `medit`/`oedit`/`redit`/`zedit` capabilities, accessible via the existing libcivetweb server at `http://localhost:4445/olc/`
- Thread-safe request/response queue: HTTP handlers (civetweb threads) enqueue requests and block on a per-request `pthread_cond_t`; the game loop drains the queue every 100ms heartbeat pulse without any lock held during MUD data access
- Authentication via existing player credentials (`load_char` + `CRYPT` password check); only immortals (`level >= LVL_IMMORT`) may log in; session tokens (32-char hex from `/dev/urandom`) stored server-side with 1-hour TTL
- REST-like JSON API: `POST /olc/login`, `POST /olc/logout`, `GET /olc/zones`, `GET/POST /olc/room/<vnum>`, `GET/POST /olc/mob/<vnum>`, `GET/POST /olc/obj/<vnum>`, `GET/POST /olc/zone/<num>/commands`
- Changes take effect immediately in the running MUD (in-memory prototype update) and write to the same files as in-game OLC (`world/mob/medit.mob`, `world/obj/oedit.obj`, `world/wld/redit.wld`, `world/zon/<zone>.zon`)
- Permission model identical to in-game OLC: zone must be CLOSED and the player's `pfilepos` must appear in `permissions.authors[]` or `permissions.editors[]`; `LVL_GRGOD+` bypasses
- Functional HTML/CSS/JS web UI: zone sidebar, room editor (name, desc, flags, sector, exits with door flags, extra descs), mob editor (all stats, ability scores, act/affect flags), object editor (type, values, extra/wear flags, affects, extra descs), zone commands table (add/remove rows)
- `src/olc.c`: removed `static` from `olc_save_mobile`, `olc_save_object`, `olc_save_room`, `zedit_save_zone_file`; declarations added to `src/olc.h`
- `src/comm.c`: `webserver_olc_heartbeat()` called every heartbeat pulse; idle-loop `select()` now uses a 100ms timeout so the OLC queue is drained even with no player connections
- `src/Makefile.in`: added `webserver_olc.o`

## Embedded Web Server (`src/webserver.c`, `src/webserver.h`)
- When `libcivetweb` is present at build time, the MUD starts a lightweight HTTP server on `127.0.0.1:4445` (localhost only; intended to be proxied by Apache for HTTPS)
- Serves the player-who-list at `http://localhost:4445/mud/who.html` by reading live MUD data structures on demand — no pre-rendered buffer, no 30-second delay
- The who page uses an HTML table with embedded CSS (styled borders, 16pt bold cells); no dependency on any external stylesheet
- Thread safety: the game loop holds a `pthread_mutex_t` during its active phase and releases it only during sleeps; the request handler acquires the same mutex, snapshots visible player data (level/class/name/title), releases the mutex, then builds and sends the response without holding the lock during network I/O
- Detection is optional: `./configure` adds `-lcivetweb` and defines `HAVE_CIVETWEB` only when the library is found; without it the code compiles identically to before (file-based `mudwho.html` fallback)
- `cnf/configure.in`, `cnf/acconfig.h`, `src/conf.h.in`, `src/Makefile.in`, `configure` updated for the new optional dependency

## Additional GMCP Modules (`src/gmcp.c`, `src/gmcp.h`)
- **Core.Ping** — server sends `Core.Ping {}` to all connected descriptors every 60 seconds (heartbeat); keeps connections alive and lets clients measure latency (`comm.c` heartbeat)
- **Core.Goodbye** — server sends `Core.Goodbye {}` immediately before closing any socket (`close_socket` in `comm.c`)
- **Char.Afflictions.List/Add/Remove** — tracks negative AFF-flag conditions (blind, cursed, poisoned, asleep, charmed) separately from spell defences; `Add` fires on first flag transition, `Remove` fires when the flag fully clears; full list sent on login/reconnect (`handler.c` `affect_to_char`/`affect_remove`, `interpreter.c`)
- **Room.Players** — sends the list of other PCs in the room to `ch` on every room entry; `Room.Players.Add`/`Room.Players.Remove` notifies all other GMCP-enabled PCs in the room when someone arrives or departs (`handler.c` `char_to_room`/`char_from_room`)
- **External.Discord.Status** — sends Discord Rich Presence data (`game`, `details` as level+class, `state` as current room name) on login and whenever `ch` changes rooms

## Locker System (`src/locker.c`, `src/locker.h`)
- Players can create named personal storage lockers (`locker create <name>`)
- Locker names must be 1–20 alphanumeric characters (configurable via `max_locker_name_length`)
- All locker commands require the player to be in a room flagged `ROOM_LOCKER`
- New `ROOM_LOCKER` room flag (bit 16); available in redit via the room-flags toggle menu
- Per-locker limits: max 2 of the same vnum (`max_locker_vnum_count`), max 100 distinct vnums (`max_locker_vnum_types`)
- Ownership limits: max 1 locker owned per player (`max_lockers_owned`); max 3 lockers shared to any player (`max_lockers_shared`)
- Owners may share access with `locker share <name> <player>` (toggling revokes); a warning about item-loss risk is printed when granting
- Containers must be empty before storing (items are stored one at a time)
- Immortal command `lcontrol { list | show <name> | delete <name> }` (GRGOD+) for administration
- Locker objects persist in `lib/plrlockers/<name>.locker`; control records in `lib/etc/lcontrol`
- All six limit variables are runtime-configurable via `lib/etc/config`

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

## GMCP — Generic MUD Communication Protocol (`src/gmcp.c`, `src/gmcp.h`)
- GMCP implemented via Telnet sub-negotiation (option 201 / 0xC9); negotiated with `IAC WILL GMCP` / `IAC DO GMCP`
- All sends are no-ops for non-GMCP clients (`d->gmcp_enabled` guard); safe to call unconditionally
- IAC byte-stuffing handled in `process_input()` via `gmcp_strip_iac()` before the normal printable-char filter
- Output uses `write_to_descriptor_n()` (binary-safe, length-based) to bypass the text pipeline
- **Modules sent:** `Core.Hello`, `Char.StatusVars`, `Char.Status`, `Char.Vitals`, `Room.Info`, `Char.Items.List/Add/Remove`, `Char.Defences.List/Add/Remove`, `Comm.Channel.Text`
- **`Char.Status`** fields: `name`, `class`, `level`, `align`, `xp`, `xp_next`, `ac` (display units, −10..+10)
- **`Char.Vitals`** fields: `hp`, `hpmax`, `mp`, `mpmax`, `mv`, `mvmax`, `gold`, `hungry` (0–24), `thirsty` (0–24)
- `Char.Status` updated on: login, reconnect, level-up, any XP gain/loss, alignment change, equip/unequip, god `advance`/`set`
- `Char.Vitals` updated on: login, reconnect, combat round, `damage()`, healing, spell mana cost, movement, god `restore`, regen tick, food/thirst condition change
- `Char.Defences` tracks active spell affects; `Add` fires only on first instance of a spell type; `Remove` fires when last instance expires
- `Comm.Channel.Text` fires for: say, tell, whisper, ask, gsay, gossip, holler, auction, congratulate, quest
- Protocol documented in `GMCP.md`

## UI / Quality of Life
- Opponent + tank status in prompt
- Improved auto-exits (immortals see more)
- Players can view spell affects and natural abilities; stats visible at level 10+
- PRF flag strings updated; autoassist SYSERR fixed
- **Equipment sets** (`eqset` command): players can save up to 5 named loadouts and switch between them; items move between worn positions and inventory without being created or destroyed; missing items reported on load; sets persist across sessions in `lib/plreqsets/`
