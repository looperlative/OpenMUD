/* ************************************************************************
*   File: gmcp.c                                        Part of CircleMUD *
*  Usage: GMCP (Generic MUD Communication Protocol) implementation        *
************************************************************************ */

#include "conf.h"
#include "sysdep.h"

#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "db.h"
#include "constants.h"
#include "spells.h"
#include "gmcp.h"

#ifdef HAVE_ARPA_TELNET_H
#include <arpa/telnet.h>
#else
#include "telnet.h"
#endif

#include <stdint.h>

/* External data */
extern const char *pc_class_types[];          /* class.c */
extern struct spell_info_type spell_info[];   /* spell_parser.c */
extern int level_exp(int chclass, int level);        /* limits.c */
extern int compute_armor_class(struct char_data *ch); /* fight.c  */

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/* Escape a string for JSON output. Returns buf. */
static const char *json_escape(const char *src, char *buf, size_t bufsz)
{
  char *d = buf;
  size_t room = bufsz - 1;

  if (!src)
    src = "";
  while (*src && room > 1) {
    switch (*src) {
    case '"':
      if (room < 2) goto done;
      *d++ = '\\'; *d++ = '"';  room -= 2; break;
    case '\\':
      if (room < 2) goto done;
      *d++ = '\\'; *d++ = '\\'; room -= 2; break;
    case '\n':
      if (room < 2) goto done;
      *d++ = '\\'; *d++ = 'n';  room -= 2; break;
    case '\r':
      if (room < 2) goto done;
      *d++ = '\\'; *d++ = 'r';  room -= 2; break;
    case '\t':
      if (room < 2) goto done;
      *d++ = '\\'; *d++ = 't';  room -= 2; break;
    default:
      *d++ = *src; room--; break;
    }
    src++;
  }
done:
  *d = '\0';
  return buf;
}

/* Send binary data of known length directly to a socket. */
static void gmcp_raw_send(struct descriptor_data *d,
                           const unsigned char *buf, size_t len)
{
  if (write_to_descriptor_n(d->descriptor, (const char *)buf, len) < 0)
    d->connected = CON_DISCONNECT;
}

/* Build and send: IAC SB GMCP <module> SP <json> IAC SE
 * If json is NULL or empty, omits the SP <json> part. */
static void gmcp_send_packet(struct descriptor_data *d,
                              const char *module,
                              const char *json)
{
  unsigned char buf[8192];
  int pos = 0;
  const char *p;

  if (!d || !d->gmcp_enabled)
    return;

  buf[pos++] = IAC;
  buf[pos++] = SB;
  buf[pos++] = TELOPT_GMCP;

  for (p = module; *p && pos < (int)sizeof(buf) - 5; p++)
    buf[pos++] = (unsigned char)*p;

  if (json && *json) {
    buf[pos++] = ' ';
    for (p = json; *p && pos < (int)sizeof(buf) - 4; p++) {
      unsigned char c = (unsigned char)*p;
      buf[pos++] = c;
      if (c == 0xFF && pos < (int)sizeof(buf) - 4)
        buf[pos++] = 0xFF;
    }
  }

  buf[pos++] = IAC;
  buf[pos++] = SE;

  gmcp_raw_send(d, buf, pos);
}

/* -----------------------------------------------------------------------
 * Telnet negotiation
 * ----------------------------------------------------------------------- */

/* Send IAC WILL GMCP — call from new_descriptor() to advertise support. */
void gmcp_send_will(struct descriptor_data *d)
{
  unsigned char will_gmcp[3] = { IAC, WILL, TELOPT_GMCP };
  gmcp_raw_send(d, will_gmcp, 3);
}

/* Called when IAC DO GMCP is received from the client. */
void gmcp_negotiate(struct descriptor_data *d)
{
  if (d->gmcp_enabled)
    return;
  d->gmcp_enabled = TRUE;
  gmcp_send_packet(d, "Core.Hello",
    "{\"name\":\"NewCirMUD\",\"version\":\"1.0\",\"auth\":false}");
}

/* Called when a complete IAC SB ... IAC SE sequence is received. */
void gmcp_handle_sb(struct descriptor_data *d)
{
  /* First byte in gmcp_sb_buf is the telnet option number (201).
   * The rest is: "Module.Name" or "Module.Name JSON". */
  d->gmcp_sb_len = 0;   /* reset accumulator; extend later if needed */
}

/* -----------------------------------------------------------------------
 * IAC state machine — strip IAC sequences from raw input
 * Returns the number of usable (non-IAC) bytes remaining in buf.
 * ----------------------------------------------------------------------- */
int gmcp_strip_iac(struct descriptor_data *d, char *buf, int len)
{
  int i, out = 0;

  for (i = 0; i < len; i++) {
    unsigned char c = (unsigned char)buf[i];

    switch (d->gmcp_iac_state) {

    case IAC_NORMAL:
      if (c == IAC)
        d->gmcp_iac_state = IAC_GOT_IAC;
      else
        buf[out++] = buf[i];
      break;

    case IAC_GOT_IAC:
      if (c == IAC) {
        buf[out++] = (char)0xFF;
        d->gmcp_iac_state = IAC_NORMAL;
      } else if (c == SB) {
        d->gmcp_sb_len = 0;
        d->gmcp_iac_state = IAC_SB_DATA;
      } else if (c >= 251 && c <= 254) {
        /* WILL/WONT/DO/DONT — need one more byte */
        d->gmcp_sb_cmd = (int)c;
        d->gmcp_iac_state = IAC_GOT_CMD;
      } else {
        /* Single-byte command (NOP, GA, etc.) */
        d->gmcp_iac_state = IAC_NORMAL;
      }
      break;

    case IAC_GOT_CMD:
      if (d->gmcp_sb_cmd == DO && c == TELOPT_GMCP)
        gmcp_negotiate(d);
      d->gmcp_iac_state = IAC_NORMAL;
      break;

    case IAC_SB_DATA:
      if (c == IAC) {
        d->gmcp_iac_state = IAC_SB_GOT_IAC;
      } else {
        if (d->gmcp_sb_len < (int)(sizeof(d->gmcp_sb_buf) - 1))
          d->gmcp_sb_buf[d->gmcp_sb_len++] = (char)c;
      }
      break;

    case IAC_SB_GOT_IAC:
      if (c == SE) {
        gmcp_handle_sb(d);
        d->gmcp_iac_state = IAC_NORMAL;
      } else if (c == IAC) {
        if (d->gmcp_sb_len < (int)(sizeof(d->gmcp_sb_buf) - 1))
          d->gmcp_sb_buf[d->gmcp_sb_len++] = (char)0xFF;
        d->gmcp_iac_state = IAC_SB_DATA;
      } else {
        d->gmcp_sb_len = 0;
        d->gmcp_iac_state = IAC_NORMAL;
      }
      break;
    }
  }

  return out;
}

/* -----------------------------------------------------------------------
 * Core lifecycle
 * ----------------------------------------------------------------------- */

void gmcp_send_ping(struct descriptor_data *d)
{
  gmcp_send_packet(d, "Core.Ping", "{}");
}

void gmcp_send_goodbye(struct descriptor_data *d)
{
  gmcp_send_packet(d, "Core.Goodbye", "{}");
}

/* -----------------------------------------------------------------------
 * Game state senders
 * ----------------------------------------------------------------------- */

void gmcp_send_char_vitals(struct char_data *ch)
{
  char json[512];

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  snprintf(json, sizeof(json),
    "{\"hp\":%d,\"hpmax\":%d,\"mp\":%d,\"mpmax\":%d,\"mv\":%d,\"mvmax\":%d,"
    "\"gold\":%d,\"hungry\":%d,\"thirsty\":%d}",
    GET_HIT(ch), GET_MAX_HIT(ch),
    GET_MANA(ch), GET_MAX_MANA(ch),
    GET_MOVE(ch), GET_MAX_MOVE(ch),
    GET_GOLD(ch),
    GET_COND(ch, FULL)   < 0 ? 24 : GET_COND(ch, FULL),
    GET_COND(ch, THIRST) < 0 ? 24 : GET_COND(ch, THIRST));

  gmcp_send_packet(ch->desc, "Char.Vitals", json);
}

void gmcp_send_char_statusvars(struct char_data *ch)
{
  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  gmcp_send_packet(ch->desc, "Char.StatusVars",
    "{\"name\":\"Char Name\",\"class\":\"Class\",\"level\":\"Level\","
    "\"align\":\"Alignment\",\"xp\":\"Experience\","
    "\"xp_next\":\"XP To Level\",\"ac\":\"Armor Class\","
    "\"gold\":\"Gold\",\"hungry\":\"Food\",\"thirsty\":\"Thirst\"}");
}

void gmcp_send_char_status(struct char_data *ch)
{
  char json[1024], ename[128], eclass[64];
  const char *align_str;

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  align_str = IS_GOOD(ch) ? "good" : IS_EVIL(ch) ? "evil" : "neutral";

  snprintf(json, sizeof(json),
	   "{\"name\":\"%s\",\"class\":\"%s\",\"level\":%d,\"align\":\"%s\","
	   "\"xp\":%d,\"xp_next\":%d,\"ac\":%d,\"alignnum\":\"%d\"}",
	   json_escape(GET_PC_NAME(ch), ename, sizeof(ename)),
	   json_escape(pc_class_types[(int)GET_CLASS(ch)], eclass, sizeof(eclass)),
	   (int)GET_LEVEL(ch),
	   align_str,
	   GET_EXP(ch),
	   level_exp(GET_CLASS(ch), GET_LEVEL(ch) + 1),
	   compute_armor_class(ch) / 10,
	   GET_ALIGNMENT(ch)
      );

  gmcp_send_packet(ch->desc, "Char.Status", json);
}

void gmcp_send_room_info(struct char_data *ch)
{
  char json[2048], exits[64], rname[256], zname[256];
  const char *dir_abbr[NUM_OF_DIRS] = { "n", "e", "s", "w", "u", "d" };
  room_rnum room;
  int i, first = 1;
  char *ep;

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  room = IN_ROOM(ch);
  if (room == NOWHERE)
    return;

  /* Build exits sub-object */
  exits[0] = '{'; exits[1] = '\0';
  ep = exits + 1;
  for (i = 0; i < NUM_OF_DIRS; i++) {
    int passable = (world[room].dir_option[i] != NULL &&
                    !IS_SET(world[room].dir_option[i]->exit_info, EX_CLOSED));
    ep += snprintf(ep, exits + sizeof(exits) - ep,
                   "%s\"%s\":%s",
                   first ? "" : ",",
                   dir_abbr[i],
                   passable ? "true" : "false");
    first = 0;
  }
  strncat(exits, "}", sizeof(exits) - strlen(exits) - 1);

  snprintf(json, sizeof(json),
    "{\"num\":%d,\"name\":\"%s\",\"zone\":\"%s\","
    "\"terrain\":\"%s\",\"exits\":%s}",
    world[room].number,
    json_escape(world[room].name, rname, sizeof(rname)),
    json_escape(zone_table[world[room].zone].name, zname, sizeof(zname)),
    sector_types[world[room].sector_type],
    exits);

  gmcp_send_packet(ch->desc, "Room.Info", json);
}

void gmcp_send_char_items_list(struct char_data *ch)
{
  char json[8192];
  char *p = json, *end = json + sizeof(json) - 4;
  struct obj_data *obj;
  int i, first = 1;
  char ename[256];

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  *p++ = '[';

  for (obj = ch->carrying; obj && p < end; obj = obj->next_content) {
    p += snprintf(p, end - p,
      "%s{\"id\":%lu,\"vnum\":%d,\"name\":\"%s\",\"type\":%d,\"worn\":-1}",
      first ? "" : ",",
      (unsigned long)(uintptr_t)obj,
      (int)GET_OBJ_VNUM(obj),
      json_escape(obj->short_description, ename, sizeof(ename)),
      (int)GET_OBJ_TYPE(obj));
    first = 0;
  }

  for (i = 0; i < NUM_WEARS && p < end; i++) {
    if ((obj = GET_EQ(ch, i)) != NULL) {
      p += snprintf(p, end - p,
        "%s{\"id\":%lu,\"vnum\":%d,\"name\":\"%s\",\"type\":%d,\"worn\":%d}",
        first ? "" : ",",
        (unsigned long)(uintptr_t)obj,
        (int)GET_OBJ_VNUM(obj),
        json_escape(obj->short_description, ename, sizeof(ename)),
        (int)GET_OBJ_TYPE(obj), i);
      first = 0;
    }
  }

  if (p < end + 3) {
    *p++ = ']';
    *p   = '\0';
  }

  gmcp_send_packet(ch->desc, "Char.Items.List", json);
}

void gmcp_send_char_items_add(struct char_data *ch, struct obj_data *obj)
{
  char json[512], ename[256];

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  snprintf(json, sizeof(json),
    "{\"id\":%lu,\"vnum\":%d,\"name\":\"%s\",\"type\":%d,\"worn\":%d}",
    (unsigned long)(uintptr_t)obj,
    (int)GET_OBJ_VNUM(obj),
    json_escape(obj->short_description, ename, sizeof(ename)),
    (int)GET_OBJ_TYPE(obj),
    (int)obj->worn_on);

  gmcp_send_packet(ch->desc, "Char.Items.Add", json);
}

void gmcp_send_char_items_remove(struct char_data *ch, struct obj_data *obj)
{
  char json[64];

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  snprintf(json, sizeof(json), "{\"id\":%lu}",
           (unsigned long)(uintptr_t)obj);

  gmcp_send_packet(ch->desc, "Char.Items.Remove", json);
}

void gmcp_send_comm_channel(struct descriptor_data *d, const char *channel,
                             const char *talker, const char *text)
{
  char json[1024], echan[64], etalker[128], etext[768];

  if (!d || !d->gmcp_enabled)
    return;

  snprintf(json, sizeof(json),
    "{\"channel\":\"%s\",\"talker\":\"%s\",\"text\":\"%s\"}",
    json_escape(channel, echan, sizeof(echan)),
    json_escape(talker,  etalker, sizeof(etalker)),
    json_escape(text,    etext,   sizeof(etext)));

  gmcp_send_packet(d, "Comm.Channel.Text", json);
}

/* -----------------------------------------------------------------------
 * Char.Defences — active spell affect tracking
 * ----------------------------------------------------------------------- */

/* Build the JSON object for a single affect (shared by List and Add). */
static int defence_json(char *buf, size_t bufsz,
                         const char *prefix,
                         struct affected_type *af)
{
  char ename[64], remaining_text[32];

  json_escape(spell_info[af->type].name, ename, sizeof(ename));

  if (af->duration == -1)
    snprintf(remaining_text, sizeof(remaining_text), "permanent");
  else
    snprintf(remaining_text, sizeof(remaining_text), "%d MUD hours", af->duration);

  return snprintf(buf, bufsz,
    "%s{\"name\":\"%s\",\"desc\":\"%s\",\"remaining\":%d,"
    "\"remaining_unit\":\"mud_hours\",\"remaining_text\":\"%s\"}",
    prefix, ename, ename, af->duration, remaining_text);
}

void gmcp_send_char_defences_list(struct char_data *ch)
{
  char json[4096];
  char *p = json, *end = json + sizeof(json) - 4;
  struct affected_type *af;
  bool seen[MAX_SPELLS + 1];
  int first = 1, i;

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  for (i = 0; i <= MAX_SPELLS; i++)
    seen[i] = FALSE;

  *p++ = '[';

  for (af = ch->affected; af && p < end; af = af->next) {
    if (af->type <= 0 || af->type > MAX_SPELLS)
      continue;
    if (seen[af->type])
      continue;
    seen[af->type] = TRUE;
    p += defence_json(p, end - p, first ? "" : ",", af);
    first = 0;
  }

  *p++ = ']';
  *p   = '\0';

  gmcp_send_packet(ch->desc, "Char.Defences.List", json);
}

void gmcp_send_char_defences_add(struct char_data *ch, struct affected_type *af)
{
  char json[256];

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  defence_json(json, sizeof(json), "", af);

  gmcp_send_packet(ch->desc, "Char.Defences.Add", json);
}

void gmcp_send_char_defences_remove(struct char_data *ch, int spell_type)
{
  char json[64], ename[64];

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  snprintf(json, sizeof(json), "\"%s\"",
    json_escape(spell_info[spell_type].name, ename, sizeof(ename)));

  gmcp_send_packet(ch->desc, "Char.Defences.Remove", json);
}

/* -----------------------------------------------------------------------
 * Char.Afflictions — negative-condition tracking (blind, poison, etc.)
 * ----------------------------------------------------------------------- */

static const struct { long bit; const char *name; } affliction_map[] = {
  { AFF_BLIND,  "blind"    },
  { AFF_CURSE,  "cursed"   },
  { AFF_POISON, "poisoned" },
  { AFF_SLEEP,  "asleep"   },
  { AFF_CHARM,  "charmed"  },
  { 0, NULL }
};

void gmcp_send_char_afflictions_list(struct char_data *ch)
{
  char json[512], *p = json, *end = json + sizeof(json) - 4;
  long aff;
  int i, first = 1;

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  aff = AFF_FLAGS(ch);
  *p++ = '[';
  for (i = 0; affliction_map[i].bit; i++) {
    if (!(aff & affliction_map[i].bit))
      continue;
    p += snprintf(p, end - p, "%s\"%s\"", first ? "" : ",", affliction_map[i].name);
    first = 0;
  }
  *p++ = ']';
  *p   = '\0';

  gmcp_send_packet(ch->desc, "Char.Afflictions.List", json);
}

void gmcp_send_char_afflictions_add(struct char_data *ch, long bits)
{
  char json[64];
  int i;

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  for (i = 0; affliction_map[i].bit; i++) {
    if (!(bits & affliction_map[i].bit))
      continue;
    snprintf(json, sizeof(json), "\"%s\"", affliction_map[i].name);
    gmcp_send_packet(ch->desc, "Char.Afflictions.Add", json);
  }
}

void gmcp_send_char_afflictions_remove(struct char_data *ch, long bits)
{
  char json[64];
  int i;

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  for (i = 0; affliction_map[i].bit; i++) {
    if (!(bits & affliction_map[i].bit))
      continue;
    snprintf(json, sizeof(json), "\"%s\"", affliction_map[i].name);
    gmcp_send_packet(ch->desc, "Char.Afflictions.Remove", json);
  }
}

/* -----------------------------------------------------------------------
 * Room.Players — player occupant list for the current room
 * ----------------------------------------------------------------------- */

/* Send the full Room.Players list to ch (called when ch enters a room). */
void gmcp_send_room_players(struct char_data *ch)
{
  char json[4096], *p = json, *end = json + sizeof(json) - 4;
  struct char_data *vict;
  char ename[128];
  int first = 1;
  room_rnum room;

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  room = IN_ROOM(ch);
  if (room == NOWHERE)
    return;

  *p++ = '[';
  for (vict = world[room].people; vict && p < end; vict = vict->next_in_room) {
    if (vict == ch || IS_NPC(vict))
      continue;
    p += snprintf(p, end - p, "%s{\"name\":\"%s\"}",
      first ? "" : ",",
      json_escape(GET_PC_NAME(vict), ename, sizeof(ename)));
    first = 0;
  }
  *p++ = ']';
  *p   = '\0';

  gmcp_send_packet(ch->desc, "Room.Players", json);
}

/* Notify existing room PCs that ch has arrived. */
void gmcp_notify_room_players_add(struct char_data *ch)
{
  char json[256], ename[128];
  struct char_data *vict;
  room_rnum room;

  if (IS_NPC(ch))
    return;

  room = IN_ROOM(ch);
  if (room == NOWHERE)
    return;

  snprintf(json, sizeof(json), "{\"name\":\"%s\"}",
           json_escape(GET_PC_NAME(ch), ename, sizeof(ename)));

  for (vict = world[room].people; vict; vict = vict->next_in_room) {
    if (vict == ch || IS_NPC(vict) || !vict->desc || !vict->desc->gmcp_enabled)
      continue;
    gmcp_send_packet(vict->desc, "Room.Players.Add", json);
  }
}

/* Notify remaining room PCs that ch is leaving. Call before removing ch. */
void gmcp_notify_room_players_remove(struct char_data *ch)
{
  char json[256], ename[128];
  struct char_data *vict;
  room_rnum room;

  if (IS_NPC(ch))
    return;

  room = IN_ROOM(ch);
  if (room == NOWHERE)
    return;

  snprintf(json, sizeof(json), "{\"name\":\"%s\"}",
           json_escape(GET_PC_NAME(ch), ename, sizeof(ename)));

  for (vict = world[room].people; vict; vict = vict->next_in_room) {
    if (vict == ch || IS_NPC(vict) || !vict->desc || !vict->desc->gmcp_enabled)
      continue;
    gmcp_send_packet(vict->desc, "Room.Players.Remove", json);
  }
}

/* -----------------------------------------------------------------------
 * External.Discord — rich-presence status for Discord integration
 * ----------------------------------------------------------------------- */

void gmcp_send_discord_status(struct char_data *ch)
{
  char json[512], rname[256], cname[64];
  room_rnum room;

  if (!ch->desc || !ch->desc->gmcp_enabled)
    return;

  room = IN_ROOM(ch);

  snprintf(json, sizeof(json),
    "{\"game\":\"NewCirMUD\",\"details\":\"Level %d %s\",\"state\":\"%s\"}",
    (int)GET_LEVEL(ch),
    json_escape(pc_class_types[(int)GET_CLASS(ch)], cname, sizeof(cname)),
    room != NOWHERE ?
      json_escape(world[room].name, rname, sizeof(rname)) : "Unknown");

  gmcp_send_packet(ch->desc, "External.Discord.Status", json);
}
