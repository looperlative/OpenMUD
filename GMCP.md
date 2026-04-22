# GMCP Support in NewCirMUD

Generic MUD Communication Protocol (GMCP) allows clients such as Mudlet and MUSHclient to receive structured game state data over the same Telnet connection used for normal play. GMCP uses Telnet sub-negotiation on option 201 (0xC9) to exchange JSON-encoded packets without interfering with the text stream.

## Protocol Negotiation

On connection the server immediately sends:

```
IAC WILL GMCP
```

If the client responds with `IAC DO GMCP`, the server enables GMCP for that session and sends `Core.Hello`. If the client does not respond (plain Telnet clients), GMCP is never enabled and no GMCP data is ever sent.

GMCP packets use the standard Telnet sub-negotiation framing:

```
IAC SB GMCP <Module.Name> <SP> <JSON> IAC SE
```

Any `0xFF` byte inside the JSON payload is doubled (`0xFF 0xFF`) per RFC 854. In practice JSON is pure ASCII so this never occurs.

## Modules: Server → Client

### `Core.Hello`

Sent once immediately after `IAC DO GMCP` is received.

```json
{"name":"NewCirMUD","version":"1.0","auth":false}
```

| Field | Type | Description |
|---|---|---|
| `name` | string | MUD name |
| `version` | string | Server version string |
| `auth` | bool | Always `false`; GMCP authentication not used |

---

### `Char.StatusVars`

Sent once at login/reconnect. Describes the *labels* for the status fields; values are in `Char.Status`.

```json
{"name":"Char Name","race":"Race","class":"Class","level":"Level","align":"Alignment"}
```

All values are literal label strings. Clients use this to build display widgets before the first `Char.Status` arrives.

---

### `Char.Status`

Sent at login, on reconnect, on level-up (via `gain_exp`, `gain_exp_regardless`, or `do_advance`), on alignment change (kill XP), and when a god uses `set` on the character.

```json
{"name":"Gandalf","class":"Magic User","level":"12","align":"good"}
```

| Field | Type | Description |
|---|---|---|
| `name` | string | Character name (PC name, not title) |
| `class` | string | Class name from `pc_class_types[]` |
| `level` | string | Current level as a decimal string |
| `align` | string | `"good"`, `"neutral"`, or `"evil"` |

`level` is sent as a string to match the `Char.StatusVars` label convention.

---

### `Char.Vitals`

Sent on: login, reconnect, each combat round (attacker and defender), every damage source via `damage()`, every healing spell via `mag_points()`, spell mana cost (success and fail paths), movement, god `restore`, and once per mud-hour regen tick.

```json
{"hp":85,"hpmax":120,"mp":40,"mpmax":200,"mv":60,"mvmax":100}
```

| Field | Type | Description |
|---|---|---|
| `hp` | int | Current hit points |
| `hpmax` | int | Maximum hit points |
| `mp` | int | Current mana |
| `mpmax` | int | Maximum mana |
| `mv` | int | Current movement points |
| `mvmax` | int | Maximum movement points |

---

### `Room.Info`

Sent whenever the character enters a room: normal movement, god `goto`, `teleport`, `summon`, `recall`, area spells that move the character, and any other path through `char_to_room()`. Also sent at reconnect.

```json
{
  "num": 3001,
  "name": "The Temple of Midgaard",
  "zone": "Midgaard",
  "terrain": "Inside",
  "exits": {"n":true,"e":false,"s":true,"w":false,"u":false,"d":false}
}
```

| Field | Type | Description |
|---|---|---|
| `num` | int | Room vnum |
| `name` | string | Room name (short description) |
| `zone` | string | Zone name from `zone_table` |
| `terrain` | string | Sector type name (e.g., `"Inside"`, `"City"`, `"Forest"`) |
| `exits` | object | Six direction keys (`n`,`e`,`s`,`w`,`u`,`d`); `true` = exit exists and is open, `false` = no exit or exit is closed/locked |

A closed door shows `false`. A locked door also shows `false`. There is no distinction between "no exit" and "closed exit" in this field.

---

### `Char.Items.List`

Sent at login and reconnect. Full snapshot of carried inventory and worn equipment.

```json
[
  {"id":140234567,"vnum":1001,"name":"a leather pouch","type":9,"worn":-1},
  {"id":140234890,"vnum":3020,"name":"a long sword","type":5,"worn":16}
]
```

| Field | Type | Description |
|---|---|---|
| `id` | uint | Per-session unique item identifier (see [Item IDs](#item-ids)) |
| `vnum` | int | Object vnum from the world database |
| `name` | string | Short description (`short_description`) |
| `type` | int | Object type flag (`ITEM_WEAPON`, `ITEM_ARMOR`, etc.) |
| `worn` | int | `-1` if carried in inventory; wear slot index if equipped |

Wear slot indices match `WEAR_*` constants in `structs.h` (0 = light, 16 = wield, etc.).

---

### `Char.Items.Add`

Sent when an item enters the character's possession: picking up an item (`obj_to_char`), or equipping an item (`equip_char`). JSON format is identical to a single element of `Char.Items.List`.

```json
{"id":140234567,"vnum":1001,"name":"a leather pouch","type":9,"worn":-1}
```

For a picked-up item, `worn` is `-1`. For an equipped item, `worn` is the slot index (set by `equip_char` before this packet is sent).

---

### `Char.Items.Remove`

Sent when an item leaves the character's possession: dropping/giving/destroying (`obj_from_char`) or unequipping (`unequip_char`).

```json
{"id":140234567}
```

| Field | Type | Description |
|---|---|---|
| `id` | uint | The same ID that was previously sent in `Char.Items.Add` or `Char.Items.List` |

---

### `Comm.Channel.Text`

Sent when a communication event occurs on any supported channel. Each participant (sender and all recipients) receives this packet on their own connection.

```json
{"channel":"gossip","talker":"Gandalf","text":"Anyone need a recall?"}
```

| Field | Type | Description |
|---|---|---|
| `channel` | string | Channel name (see table below) |
| `talker` | string | PC name of the speaker |
| `text` | string | Message text as typed (no color codes) |

Supported channels:

| Channel string | Trigger |
|---|---|
| `say` | `say` command; sent to everyone in the room including the speaker |
| `tell` | `tell` command; sent to both sender and recipient |
| `whisper` | `whisper` command; sent to both sender and recipient |
| `ask` | `ask` command; sent to both sender and recipient |
| `gsay` | Group say; sent to all group members including the speaker |
| `gossip` | Gossip channel |
| `holler` | Holler channel |
| `auction` | Auction channel |
| `congratulate` | Gratz channel |
| `quest` | Quest channel |

---

### `Char.Defences.List`

Sent at login and reconnect. Array of all currently active spell affects, one entry per unique spell type (multi-affect spells such as `bless` appear as one entry).

```json
[
  {"name":"armor","desc":"armor","remaining":24,"remaining_unit":"mud_hours","remaining_text":"24 MUD hours"},
  {"name":"bless","desc":"bless","remaining":6,"remaining_unit":"mud_hours","remaining_text":"6 MUD hours"}
]
```

| Field | Type | Description |
|---|---|---|
| `name` | string | Spell name (lowercase, e.g. `"armor"`, `"bless"`, `"curse"`) |
| `desc` | string | Same as `name` (IRE convention; extended fields carry detail) |
| `remaining` | int | Remaining duration in mud hours; `-1` = permanent (god-only) |
| `remaining_unit` | string | Always `"mud_hours"` |
| `remaining_text` | string | Human-readable form, e.g. `"6 MUD hours"` or `"permanent"` |

---

### `Char.Defences.Add`

Sent when a spell affect is applied and no instance of that spell was previously active. Object shape is identical to one element of `Char.Defences.List`.

```json
{"name":"armor","desc":"armor","remaining":24,"remaining_unit":"mud_hours","remaining_text":"24 MUD hours"}
```

Re-casting an already-active spell (duration refresh) does **not** send a new `Char.Defences.Add` — the defence is already in the client's list.

---

### `Char.Defences.Remove`

Sent when the last instance of a spell affect is removed. Plain string per IRE convention.

```json
"armor"
```

Triggers: spell expiry (`affect_update`), cure spells (`remove curse`, `cure blindness`, etc.), character death (all affects stripped), or god commands.

---

## Modules: Client → Server

### `Core.Supports.Set`

The server receives and parses the Telnet sub-negotiation framing for client-sent GMCP packets. However, `Core.Supports.Set` is currently accepted but not acted upon — the server sends all supported modules unconditionally regardless of what the client declares. This has no practical effect since all standard GMCP clients declare support for the same modules the server sends.

---

## Item IDs

Item IDs are the C pointer address of the `obj_data` structure cast to `unsigned long`. They are unique within a session and stable for the lifetime of the object in memory, but are not persistent across server restarts or rents. Clients should treat them as opaque handles valid only for the current session.

---

## Trigger Summary

| Module | Triggers |
|---|---|
| `Core.Hello` | Once, on `IAC DO GMCP` |
| `Char.StatusVars` | Login, reconnect |
| `Char.Status` | Login, reconnect, level-up, alignment change, god `advance`/`set` |
| `Char.Vitals` | Login, reconnect, combat round (both sides), any `damage()` call, healing spell, spell cast (mana cost), movement, god `restore`, regen tick |
| `Room.Info` | Any `char_to_room()` call, reconnect |
| `Char.Items.List` | Login, reconnect |
| `Char.Items.Add` | `obj_to_char()`, `equip_char()` |
| `Char.Items.Remove` | `obj_from_char()`, `unequip_char()` |
| `Char.Defences.List` | Login, reconnect |
| `Char.Defences.Add` | Spell affect applied (first instance of that spell type only) |
| `Char.Defences.Remove` | Last instance of a spell affect removed (expiry, cure, death, god purge) |
| `Comm.Channel.Text` | say, tell, whisper, ask, gsay, gossip, holler, auction, congratulate, quest |

---

## Configuring Mudlet

1. Open the **GMCP** tab in the Mudlet settings, or use the **Script Editor**.
2. Enable GMCP: `msdp.sendRequest("Core.Supports.Set", {"Char.Vitals 1", "Room.Info 1", "Char.Status 1"})` — though the server sends all modules regardless.
3. Register handlers with `registerAnonymousEventHandler("gmcp.Char.Vitals", function() ... end)`.
4. GMCP data is available in Mudlet as the `gmcp` table: `gmcp.Char.Vitals.hp`, `gmcp.Room.Info.name`, etc.
