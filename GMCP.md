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

### `Core.Ping`

Sent to every connected descriptor every 60 real seconds. Clients may use this to measure round-trip latency; the payload is always an empty object.

```json
{}
```

No response is required from the client.

---

### `Core.Goodbye`

Sent immediately before the server closes a connection (player `quit`, link-death, idle disconnect, god `boot`, etc.). Allows clients to distinguish a clean server-side close from a dropped connection.

```json
{}
```

---

### `Char.StatusVars`

Sent once at login/reconnect. Describes the *labels* for the status fields; values are in `Char.Status` and `Char.Vitals`.

```json
{"name":"Char Name","class":"Class","level":"Level","align":"Alignment",
 "xp":"Experience","xp_next":"XP To Level","ac":"Armor Class",
 "gold":"Gold","hungry":"Food","thirsty":"Thirst"}
```

All values are literal label strings. Clients use this to build display widgets before the first `Char.Status` arrives.

---

### `Char.Status`

Sent at login, on reconnect, on level-up, on any XP gain or loss, on alignment change, on equip/unequip, and when a god uses `set` on the character.

```json
{"name":"Gandalf","class":"Magic User","level":12,"align":"good",
 "xp":45230,"xp_next":75000,"ac":-3}
```

| Field | Type | Description |
|---|---|---|
| `name` | string | Character name (PC name, not title) |
| `class` | string | Class name from `pc_class_types[]` |
| `level` | int | Current level |
| `align` | string | `"good"`, `"neutral"`, or `"evil"` |
| `xp` | int | Current experience points |
| `xp_next` | int | Experience required to reach the next level |
| `ac` | int | Armor class in display units (−10 to +10); lower is better. Includes DEX defensive bonus. Equivalent to the value shown by the `score` command. |

---

### `Char.Vitals`

Sent on: login, reconnect, each combat round (attacker and defender), every damage source via `damage()`, every healing spell via `mag_points()`, spell mana cost (success and fail paths), movement, god `restore`, regen tick, and whenever the food or thirst condition changes.

```json
{"hp":85,"hpmax":120,"mp":40,"mpmax":200,"mv":60,"mvmax":100,
 "gold":1250,"hungry":18,"thirsty":12}
```

| Field | Type | Description |
|---|---|---|
| `hp` | int | Current hit points |
| `hpmax` | int | Maximum hit points |
| `mp` | int | Current mana |
| `mpmax` | int | Maximum mana |
| `mv` | int | Current movement points |
| `mvmax` | int | Maximum movement points |
| `gold` | int | Gold coins currently carried (not bank gold) |
| `hungry` | int | Food level 0–24; 0 = hungry, 24 = full. Immortals always report 24. |
| `thirsty` | int | Thirst level 0–24; 0 = thirsty, 24 = quenched. Immortals always report 24. |

---

### `Char.Afflictions.List`

Sent at login and reconnect. Array of currently active affliction names. Only the five AFF flags tracked as afflictions are included (see table under `Char.Afflictions.Add`). An empty array means the character has no active afflictions.

```json
["poisoned","blind"]
```

---

### `Char.Afflictions.Add`

Sent when an affliction flag transitions from absent to present — i.e., the first spell affect that sets the flag is applied. One packet per affliction name.

```json
"poisoned"
```

Tracked affliction flags:

| AFF flag | Name sent |
|---|---|
| `AFF_BLIND` | `"blind"` |
| `AFF_CURSE` | `"cursed"` |
| `AFF_POISON` | `"poisoned"` |
| `AFF_SLEEP` | `"asleep"` |
| `AFF_CHARM` | `"charmed"` |

Note: `Char.Defences.Add` is also sent for any spell affect regardless of type. `Char.Afflictions.Add` is the client signal that the *condition* (AFF flag) is newly active.

---

### `Char.Afflictions.Remove`

Sent when an affliction flag transitions from present to absent — i.e., the last spell affect holding that flag is removed. One packet per affliction name. Plain string per IRE convention.

```json
"poisoned"
```

Triggers: spell expiry (`affect_update`), cure spells (`cure poison`, `remove curse`, `cure blindness`, etc.), character death (all affects stripped), or god commands.

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

### `Room.Players`

Sent to `ch` when they enter a room. Array of all other player characters currently in the room, excluding `ch` themselves and all NPCs. An empty array means no other players are present.

```json
[{"name":"Aragorn"},{"name":"Legolas"}]
```

| Field | Type | Description |
|---|---|---|
| `name` | string | PC name of the other player |

---

### `Room.Players.Add`

Sent to every GMCP-enabled PC already in a room when a new player character enters. Carries only the newcomer's name.

```json
{"name":"Gandalf"}
```

---

### `Room.Players.Remove`

Sent to every GMCP-enabled PC remaining in a room when a player character departs (movement, teleport, link-death, quit, etc.).

```json
{"name":"Gandalf"}
```

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

### `External.Discord.Status`

Sent at login, reconnect, and on every room entry. Provides Discord Rich Presence data for clients (Mudlet, etc.) that integrate with Discord.

```json
{"game":"NewCirMUD","details":"Level 12 Magic User","state":"The Temple of Midgaard"}
```

| Field | Type | Description |
|---|---|---|
| `game` | string | Always `"NewCirMUD"` |
| `details` | string | `"Level N <class>"` — current level and class name |
| `state` | string | Current room name; `"Unknown"` if the character has no location |

Mudlet handles this packet automatically when the Discord integration is enabled in Mudlet's settings.

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
| `Core.Ping` | Every 60 real seconds, to all connected descriptors |
| `Core.Goodbye` | Immediately before any socket close (quit, link-death, idle, god boot) |
| `Char.StatusVars` | Login, reconnect |
| `Char.Status` | Login, reconnect, level-up, any XP gain/loss, alignment change, equip/unequip, god `advance`/`set` |
| `Char.Vitals` | Login, reconnect, combat round (both sides), any `damage()` call, healing spell, spell cast (mana cost), movement, god `restore`, regen tick, food/thirst condition change |
| `Char.Afflictions.List` | Login, reconnect |
| `Char.Afflictions.Add` | AFF flag transitions 0→1 (first spell holding that flag applied) |
| `Char.Afflictions.Remove` | AFF flag transitions 1→0 (last spell holding that flag removed) |
| `Room.Info` | Any `char_to_room()` call, reconnect |
| `Room.Players` | Any `char_to_room()` call (sent to the arriving character), reconnect |
| `Room.Players.Add` | A PC enters a room (sent to all other GMCP-enabled PCs already in the room) |
| `Room.Players.Remove` | A PC leaves a room (sent to all GMCP-enabled PCs remaining in the room) |
| `Char.Items.List` | Login, reconnect |
| `Char.Items.Add` | `obj_to_char()`, `equip_char()` |
| `Char.Items.Remove` | `obj_from_char()`, `unequip_char()` |
| `Char.Defences.List` | Login, reconnect |
| `Char.Defences.Add` | Spell affect applied (first instance of that spell type only) |
| `Char.Defences.Remove` | Last instance of a spell affect removed (expiry, cure, death, god purge) |
| `Comm.Channel.Text` | say, tell, whisper, ask, gsay, gossip, holler, auction, congratulate, quest |
| `External.Discord.Status` | Login, reconnect, any `char_to_room()` call |

---

## Configuring Mudlet

1. Open the **GMCP** tab in the Mudlet settings, or use the **Script Editor**.
2. Enable GMCP: `msdp.sendRequest("Core.Supports.Set", {"Char.Vitals 1", "Room.Info 1", "Char.Status 1"})` — though the server sends all modules regardless.
3. Register handlers with `registerAnonymousEventHandler("gmcp.Char.Vitals", function() ... end)`.
4. GMCP data is available in Mudlet as the `gmcp` table: `gmcp.Char.Vitals.hp`, `gmcp.Room.Info.name`, etc.
5. For Discord integration, enable **Discord Rich Presence** in Mudlet's preferences. Mudlet handles `External.Discord.Status` automatically once enabled.
