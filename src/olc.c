/* OLC.C
 *
 * This file contains my OLC system for CircleMUD.  I'm providing my
 * code with a more lenient MIT-style specified below.
 *
 * Copyright 2024 - Robert Amstadt
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the “Software”), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "interpreter.h"
#include "utils.h"
#include "db.h"
#include "comm.h"
#include "screen.h"
#include "constants.h"
#include "spells.h"
#include "olc.h"

extern int num_allocated_zone;

static struct olc_editor_s olc_editors[MAX_EDITORS];
static struct olc_garbage_s *olc_mob_garbage = NULL;
static struct olc_garbage_s *olc_obj_garbage = NULL;

const char *olc_zone_flags[] =
{
    "CLOSED",
    "LOCKED",
    "\n"
};

const char *attack_types[] =
{
    "hits",              /* 0 */
    "stings",
    "whips",
    "slashes",
    "bites",
    "bludgeons",    /* 5 */
    "crushes",
    "pounds",
    "claws",
    "mauls",
    "thrashes",       /* 10 */
    "pierces",
    "blasts",
    "punches",
    "stabs",
    "\n"
};

static void olc_save_mobile(int vnum);
static void olc_save_object(int vnum);
static void olc_save_room(int vnum);

static void olc_state_after_push(struct olc_editor_s *ed, int state)
{
    ed->state_after[1] = ed->state_after[0];
    ed->state_after[0] = state;
}

static int olc_state_after_pop(struct olc_editor_s *ed)
{
    int state = ed->state_after[0];
    ed->state_after[0] = ed->state_after[1];
    ed->state_after[1] = 0;
    return state;
}

static void olc_free(struct olc_editor_s *ed, void *junk)
{
    if (ed->garbage_list == NULL)
	free(junk);
    else
    {
	struct olc_garbage_s *piece = (struct olc_garbage_s *) malloc(sizeof *piece);
	piece->vnum = ed->vnum;
	piece->garbage = junk;
	piece->next = *ed->garbage_list;
	*ed->garbage_list = piece;
    }
}

static void olc_save(struct olc_editor_s *ed)
{
    if (ed->vnum > 0)
    {
	switch(ed->edit_type)
	{
	  case OLC_EDIT_MOBILE:
	    olc_save_mobile(ed->vnum);
	    break;
	  case OLC_EDIT_OBJECT:
	    olc_save_object(ed->vnum);
	    break;
	  case OLC_EDIT_ROOM:
	    olc_save_room(ed->vnum);
	    break;
	}
    }
}

static void olc_clear_editor(int idx)
{
    struct olc_editor_s *ed = &olc_editors[idx];

    olc_save(ed);

    // Should free any allocated memory held by this editor.

    // Clear the editor.
    memset(ed, 0, sizeof(*ed));
}

static void olc_create_editor(struct char_data *ch)
{
    ch->desc->olc_editor_idx = 0;

    for (int i = 0; i < MAX_EDITORS; i++)
    {
	if (olc_editors[i].idnum == 0)
	{
	    memset(olc_editors + i, 0, sizeof(olc_editors[i]));
	    olc_editors[i].idnum = GET_IDNUM(ch);
	    olc_editors[i].state = 0;
	    ch->desc->olc_editor_idx = i;
	}
    }
}

void olc_get_number(struct descriptor_data *d, struct olc_editor_s *ed,
		    char *prompt, void *returnval, int returnsize,
		    int min, int max, int returnstate)
{
    ed->number_field = returnval;
    ed->number_field_size = returnsize;
    ed->number_min = min;
    ed->number_max = max;
    ed->state = OLC_STATE_NUMBER;
    olc_state_after_push(ed, returnstate);
    send_to_char(d->character, "%s", prompt);
}

void olc_handle_toggleedit(struct descriptor_data *d, struct olc_editor_s *ed, char *arg)
{
    struct char_data *ch = d->character;
    char buf[MAX_STRING_LENGTH];

    if (*arg == '.')
    {
	ed->flags_field = NULL;
	ed->bit_names = NULL;
	ed->n_bits = 0;
	ed->field_name = NULL;
	ed->state = olc_state_after_pop(ed);
	olc_nanny(d, "");
	return;
    }
    else
    {
	int n = atoi(arg);
	if (n >= 1 && n <= ed->n_bits && ed->bit_names[n-1][0] != '*' &&
	    strcmp(ed->bit_names[n-1], "DEAD") != 0 &&
	    strcmp(ed->bit_names[n-1], "ISNPC") != 0)
	{
	    *ed->flags_field ^= (1 << (n - 1));
	}
    }

    sprintbit(*ed->flags_field, ed->bit_names, buf, sizeof(buf));
    send_to_char(ch, "Current %s: %s", ed->field_name, buf);

    int i = 0;
    for ( ; *ed->bit_names[i] != '\n'; i++)
    {
	if ((i & 3) == 0)
	    send_to_char(ch, "\r\n  ");
	if (*ed->bit_names[i] == '*' || strcmp(ed->bit_names[i], "DEAD") == 0 ||
	    strcmp(ed->bit_names[i], "ISNPC") == 0)
	    continue;

	send_to_char(ch, "%2d) %-16s ", i + 1, ed->bit_names[i]);
    }
    ed->n_bits = i;

    send_to_char(ch, "\r\nSelect bit to toggle or '.' to end: ");
}

void olc_start_toggleedit(struct descriptor_data *d, struct olc_editor_s *ed,
			  char *field_name, int *flags_field, const char *bit_names[])
{
    ed->field_name = field_name;
    ed->bit_names = bit_names;
    ed->flags_field = flags_field;
    ed->state = OLC_STATE_TOGGLEEDIT;
    ed->n_bits = 0;

    olc_handle_toggleedit(d, ed, "");
}

void olc_handle_typeedit(struct descriptor_data *d, struct olc_editor_s *ed, char *arg)
{
    struct char_data *ch = d->character;
    char buf[MAX_STRING_LENGTH];

    if (*arg == '.')
    {
	ed->flags_field = NULL;
	ed->flags_field8 = NULL;
	ed->bit_names = NULL;
	ed->n_bits = 0;
	ed->field_name = NULL;
	ed->state = olc_state_after_pop(ed);
	olc_nanny(d, "");
	return;
    }
    else
    {
	int n = atoi(arg);
	if (n >= 1 && n <= ed->n_bits && ed->bit_names[n-1][0] != '*')
	{
	    if (ed->flags_field != NULL)
		*ed->flags_field = n - 1;
	    else if (ed->flags_field8 != NULL)
		*ed->flags_field8 = n - 1;

	    ed->flags_field = NULL;
	    ed->flags_field8 = NULL;
	    ed->bit_names = NULL;
	    ed->n_bits = 0;
	    ed->field_name = NULL;
	    ed->state = olc_state_after_pop(ed);
	    olc_nanny(d, "");
	    return;
	}
    }

    if (ed->flags_field != NULL)
	sprinttype(*ed->flags_field, ed->bit_names, buf, sizeof(buf));
    else
	sprinttype(*ed->flags_field8, ed->bit_names, buf, sizeof(buf));
    send_to_char(ch, "Current %s: %s", ed->field_name, buf);

    int i = 0;
    for ( ; *ed->bit_names[i] != '\n'; i++)
    {
	if ((i & 3) == 0)
	    send_to_char(ch, "\r\n  ");
	if (*ed->bit_names[i] == '*')
	    continue;

	send_to_char(ch, "%2d) %-16s ", i + 1, ed->bit_names[i]);
    }
    ed->n_bits = i;

    send_to_char(ch, "\r\nSelect new type or '.' to end: ");
}

void olc_start_typeedit(struct descriptor_data *d, struct olc_editor_s *ed,
			char *field_name, int *type_field, const char *type_names[])
{
    ed->field_name = field_name;
    ed->bit_names = type_names;
    ed->flags_field = type_field;
    ed->flags_field8 = NULL;
    ed->state = OLC_STATE_TYPEEDIT;
    ed->n_bits = 0;

    olc_handle_typeedit(d, ed, "");
}

void olc_start_typeedit8(struct descriptor_data *d, struct olc_editor_s *ed,
			 char *field_name, byte *type_field, const char *type_names[])
{
    ed->field_name = field_name;
    ed->bit_names = type_names;
    ed->flags_field8 = type_field;
    ed->flags_field = NULL;
    ed->state = OLC_STATE_TYPEEDIT;
    ed->n_bits = 0;

    olc_handle_typeedit(d, ed, "");
}

void olc_start_textedit(struct descriptor_data *d, struct olc_editor_s *ed,
			char *field_name, char **string, int is_single_line, int want_dice)
{
    struct char_data *ch = d->character;

    ed->field_name = field_name;
    ed->text_edit_string = string;
    if (!want_dice && *ed->text_edit_string != NULL)
    {
	olc_free(ed, *ed->text_edit_string);
	*ed->text_edit_string = strdup("");
    }

    ed->single_line = is_single_line;
    ed->want_dice = want_dice;
    ed->want_spellname = 0;
    ed->state = OLC_STATE_TEXTEDIT;

    send_to_char(ch, "Enter new value for '%s'%s",
		 field_name,
		 is_single_line ? ":\r\n" : "\r\n(enter . at start of line to end editting):\r\n");
}

void olc_start_getspellname(struct descriptor_data *d, struct olc_editor_s *ed, int *field)
{
    struct char_data *ch = d->character;

    ed->flags_field = field;
    ed->field_name = NULL;
    ed->text_edit_string = NULL;
    ed->single_line = 1;
    ed->want_dice = 0;
    ed->want_spellname = 1;
    ed->state = OLC_STATE_TEXTEDIT;

    send_to_char(ch, "Enter the name of a spell: ");
}

void olc_handle_textedit(struct descriptor_data *d, struct olc_editor_s *ed, char *arg)
{
    char *p;
    char *more = strdup(arg);
    if (more[0] == '#')
	more[0] = ' ';
    while ((p = strchr(more, '~')) != NULL)
	*p = '-';

    if (ed->single_line || ed->want_dice)
    {
	if (ed->want_dice)
	{
	    int i1, i2, i3;
	    if (sscanf(more, " %dd%d+%d", &i1, &i2, &i3) == 3 &&
		i1 >= 1 && i1 <= 255 && i2 >=1 && i2 <= 32767 && i3 >= 1 &&
		(i3 < 256 || (i3 <= 32767 && ed->diceextra32 != NULL)))
	    {
		*ed->ndice = i1;
		*ed->sizedice = i2;
		if (ed->diceextra32 != NULL)
		    *ed->diceextra32 = i3;
		else
		    *ed->diceextra8 = i3;
	    }
	    free(more);
	}
	else if (ed->want_spellname)
	{
	    int i = find_skill_num(more);
	    if (i >= 1 && i <= TOP_SPELL_DEFINE)
		*ed->flags_field = i;
	    free(more);
	}
	else
	{
	    if (*ed->text_edit_string != NULL)
		olc_free(ed, *ed->text_edit_string);
	    *ed->text_edit_string = more;
	}

    	ed->field_name = NULL;
	ed->text_edit_string = NULL;
	ed->single_line = 0;
	ed->want_dice = 0;
	ed->want_spellname = 0;
	ed->state = olc_state_after_pop(ed);
	olc_nanny(d, "");
    }
    else if (*more == '.')
    {
	free(more);
	ed->field_name = NULL;
	ed->text_edit_string = NULL;
	ed->single_line = 0;
	ed->want_dice = 0;
	ed->state = olc_state_after_pop(ed);
	olc_nanny(d, "");
    }
    else
    {
	if (*ed->text_edit_string == NULL)
	    *ed->text_edit_string = strdup("");

	char *s1 = *ed->text_edit_string;
	CREATE(*ed->text_edit_string, char, strlen(s1) + strlen(more) + 3);
	strcpy(*ed->text_edit_string, s1);
	strcat(*ed->text_edit_string, more);
	strcat(*ed->text_edit_string, "\r\n");
	free(more);
    }
}

void olc_extradesc_display_top(struct descriptor_data *d, struct olc_editor_s *ed)
{
    struct char_data *ch = d->character;

    if (ed->extra_desc == NULL)
    {
	CREATE(ed->extra_desc, struct extra_descr_data, 1);
	ed->extra_desc->keyword = strdup("");
	ed->extra_desc->description = strdup("");
	ed->extra_desc->next = *ed->extra_desc_list;
	*ed->extra_desc_list = ed->extra_desc;
    }

    send_to_char(ch, "%sExtra description:\r\n",
		 CCCYN(ch, C_NRM));
    send_to_char(ch, " 1) Keywords: %s%s\r\n", CCNRM(ch, C_NRM), ed->extra_desc->keyword);
    send_to_char(ch, "%s 2) Description:%s\r\n%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 ed->extra_desc->description);

    send_to_char(ch, "\r\nEnter Choice (or . when done): ");
    ed->state =  OLC_STATE_EXTRADESC_TOPCHOICE;
}

void olc_extradesc_handle_top(struct descriptor_data *d, struct olc_editor_s *ed, char *arg)
{
    struct char_data *ch = d->character;

    if (!*arg)
    {
	send_to_char(ch, "Not a valid choice, try again: ");
	return;
    }

    switch (*arg)
    {
      case '1':
	olc_state_after_push(ed, OLC_STATE_EXTRADESC_TOP);
	olc_start_textedit(d, ed, "keywords", &ed->extra_desc->keyword, 1, 0);
	break;
      case '2':
	olc_state_after_push(ed, OLC_STATE_EXTRADESC_TOP);
	olc_start_textedit(d, ed, "description", &ed->extra_desc->description, 0, 0);
	break;
      case '.':
	ed->state = olc_state_after_pop(ed);
	olc_nanny(d, "");
	break;
      default:
	send_to_char(ch, "%c isn't a valid choice.\r\n", *arg);
	olc_extradesc_display_top(d, ed);
	break;
    }
}

static char directions[6] = "NESWUD";

void olc_direction_display_top(struct descriptor_data *d, struct olc_editor_s *ed, int direction)
{
    char buf[MAX_STRING_LENGTH];

    struct char_data *ch = d->character;
    int room_rnum = real_room(ed->vnum);
    struct room_data *room = &world[room_rnum];

    ed->direction = direction;
    struct room_direction_data *exit = room->dir_option[ed->direction];
    if (exit == NULL)
    {
	CREATE(exit, struct room_direction_data, 1);
	exit->general_description = strdup("");
	exit->keyword = strdup("");
	exit->to_room = NOWHERE;
	room->dir_option[ed->direction] = exit;
    }

    send_to_char(ch, "%sExit %c%s\r\n",
		 CCCYN(ch, C_NRM), directions[ed->direction], CCNRM(ch, C_NRM));
    send_to_char(ch, "%s 1) Description:%s\r\n%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 exit->general_description);
    send_to_char(ch, "%s 2) Keywords: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), exit->keyword);
    sprintbit(exit->exit_info, exit_bits, buf, sizeof(buf));
    send_to_char(ch, "%s 3) Flags: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
    send_to_char(ch, "%s 4) Key Number: %s%d\r\n",
		 CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), exit->key);
    send_to_char(ch, "%s 5) To Room: %s%d - %s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 world[exit->to_room].number, world[exit->to_room].name);

    send_to_char(ch, "\r\nEnter Choice (or . when done): ");
    ed->state =  OLC_STATE_DIRECTION_TOPCHOICE;
}

void olc_direction_handle_top(struct descriptor_data *d, struct olc_editor_s *ed, char *arg)
{
    struct char_data *ch = d->character;
    int room_rnum = real_room(ed->vnum);
    struct room_data *room = &world[room_rnum];
    struct room_direction_data *exit = room->dir_option[ed->direction];

    if (!*arg)
    {
	send_to_char(ch, "Not a valid choice, try again: ");
	return;
    }

    switch (*arg)
    {
      case '1':
	olc_state_after_push(ed, OLC_STATE_DIRECTION_TOP);
	olc_start_textedit(d, ed, "description", &exit->general_description, 0, 0);
	break;
      case '2':
	olc_state_after_push(ed, OLC_STATE_DIRECTION_TOP);
	olc_start_textedit(d, ed, "keywords", &exit->keyword, 1, 0);
	break;
      case '3':
	olc_state_after_push(ed, OLC_STATE_DIRECTION_TOP);
	olc_start_toggleedit(d, ed, "exit info", &exit->exit_info, exit_bits);
	break;
      case '4':
	send_to_char(ch, "Enter key number: ");
	ed->state = OLC_STATE_DOOR_KEY_NUMBER;
	break;
      case '5':
	send_to_char(ch, "Enter room number: ");
	ed->state = OLC_STATE_DOOR_TO_ROOM;
	break;
      case '.':
	ed->state = OLC_STATE_REDIT_TOP;
	olc_nanny(d, "");
	break;
      default:
	send_to_char(ch, "%c isn't a valid choice.\r\n", *arg);
	olc_direction_display_top(d, ed, ed->direction);
	break;
    }
}

void olc_medit_display_top(struct descriptor_data *d, struct olc_editor_s *ed)
{
    char buf[MAX_STRING_LENGTH];

    struct char_data *ch = d->character;
    int rnum = real_mobile(ed->vnum);
    struct char_data *mob = &mob_proto[rnum];

    MOB_FLAGS(mob) &= ~MOB_NOTDEADYET;
    MOB_FLAGS(mob) |= MOB_ISNPC;
    send_to_char(ch, "%sMobile %d%s\r\n", CCCYN(ch, C_NRM), ed->vnum, CCNRM(ch, C_NRM));
    send_to_char(ch, "%s 1) Aliases: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->player.name);
    send_to_char(ch, "%s 2) Short Description:%s %s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->player.short_descr);
    send_to_char(ch, "%s 3) Long Description:%s\r\n%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->player.long_descr);
    send_to_char(ch, "%s 4) Detailed Description:%s\r\n%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->player.description);
    sprintbit(MOB_FLAGS(mob), action_bits, buf, sizeof(buf));
    send_to_char(ch, "%s 5) Action Flags: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
    sprintbit(AFF_FLAGS(mob), affected_bits, buf, sizeof(buf));
    send_to_char(ch, "%s 6) Affected: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
    send_to_char(ch, "%s 7) Alignment: %s%d\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 GET_ALIGNMENT(mob));
    send_to_char(ch, "%s 8) Level: %s%d\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 GET_LEVEL(mob));
    send_to_char(ch, "%s 9) To Hit AC0: %s%d\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 GET_HITROLL(mob));
    send_to_char(ch, "%s10) Armor Class: %s%d\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 GET_AC(mob));
    send_to_char(ch, "%s11) Hitpoint Dice: %s%dd%d+%d\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->mob_specials.hpnodice, mob->mob_specials.hpsizedice,
		 mob->mob_specials.hpextra);
    send_to_char(ch, "%s12) Barehand Damage: %s%dd%d+%d\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->mob_specials.damnodice, mob->mob_specials.damsizedice, GET_DAMROLL(mob));
    send_to_char(ch, "%s13) Gold: %s%d\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 GET_GOLD(mob));
    send_to_char(ch, "%s14) Experience: %s%d\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 GET_EXP(mob));

    sprinttype(GET_POS(mob), position_types, buf, sizeof(buf));
    send_to_char(ch, "%s15)  Load Position: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
    sprinttype(GET_DEFAULT_POS(mob), position_types, buf, sizeof(buf));
    send_to_char(ch, "%s16)  Default Position: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
    sprinttype(GET_SEX(mob), genders, buf, sizeof(buf));
    send_to_char(ch, "%s17)  Gender: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
    sprinttype(mob->mob_specials.attack_type, attack_types, buf, sizeof(buf));
    send_to_char(ch, "%s18)  Attack Types: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);

    send_to_char(ch, "%s19) Str: %s%d    ", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->real_abils.str);
    send_to_char(ch, "%s20) StrAdd: %s%d    ", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->real_abils.str_add);
    send_to_char(ch, "%s21) Int: %s%d    ", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->real_abils.intel);
    send_to_char(ch, "%s22) Wis: %s%d\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->real_abils.wis);
    send_to_char(ch, "%s23) Dex: %s%d    ", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->real_abils.dex);
    send_to_char(ch, "%s24) Con: %s%d    ", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->real_abils.con);
    send_to_char(ch, "%s25) Cha: %s%d    ", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 mob->real_abils.cha);

    send_to_char(ch, "\r\nEnter Choice (or . when done): ");
    ed->state =  OLC_STATE_MEDIT_TOPCHOICE;
}

void olc_medit_handle_top(struct descriptor_data *d, struct olc_editor_s *ed, char *arg)
{
    struct char_data *ch = d->character;
    int rnum = real_mobile(ed->vnum);
    struct char_data *mob = &mob_proto[rnum];

    if (!*arg)
    {
	send_to_char(ch, "Not a valid choice, try again: ");
	return;
    }

    int iarg = atoi(arg);

    if (*arg == '.') {
	olc_clear_editor(d->olc_editor_idx);
	STATE(d) = CON_PLAYING;
	REMOVE_BIT(PLR_FLAGS(ch), PLR_WRITING);
    }
    else if (iarg == 1) {
	olc_state_after_push(ed, OLC_STATE_MEDIT_TOP);
	olc_start_textedit(d, ed, "name", &mob->player.name, 1, 0);
    }
    else if (iarg == 2) {
	olc_state_after_push(ed, OLC_STATE_MEDIT_TOP);
	olc_start_textedit(d, ed, "short desc", &mob->player.short_descr, 1, 0);
    }
    else if (iarg == 3) {
	olc_state_after_push(ed, OLC_STATE_MEDIT_TOP);
	olc_start_textedit(d, ed, "long desc", &mob->player.long_descr, 0, 0);
    }
    else if (iarg == 4) {
	olc_state_after_push(ed, OLC_STATE_MEDIT_TOP);
	olc_start_textedit(d, ed, "detailed desc", &mob->player.description, 0, 0);
    }
    else if (iarg == 5) {
	olc_state_after_push(ed, OLC_STATE_MEDIT_TOP);
	olc_start_toggleedit(d, ed, "action flags", (int *) &MOB_FLAGS(mob), action_bits);
    }
    else if (iarg == 6) {
	olc_state_after_push(ed, OLC_STATE_MEDIT_TOP);
	olc_start_toggleedit(d, ed, "affected", (int *) &AFF_FLAGS(mob), affected_bits);
    }
    else if (iarg == 7) {
	olc_get_number(d, ed, "Alignment (-1000 - 1000): ", &GET_ALIGNMENT(mob),
		       sizeof(GET_ALIGNMENT(mob)), -1000, 1000, OLC_STATE_MEDIT_TOP);
    }
    else if (iarg == 8) {
	olc_get_number(d, ed, "Level (0-100): ", &GET_LEVEL(mob),
		       sizeof(GET_LEVEL(mob)), 0, 100, OLC_STATE_MEDIT_TOP);
    }
    else if (iarg == 9) {
	olc_get_number(d, ed, "To Hit AC0: (0-20): ", &GET_HITROLL(mob),
		       sizeof(GET_HITROLL(mob)), 0, 20, OLC_STATE_MEDIT_TOP);
    }
    else if (iarg == 10) {
	olc_get_number(d, ed, "Alignment (-1000 - 1000): ", &GET_AC(mob),
		       sizeof(GET_AC(mob)), -10, 10, OLC_STATE_MEDIT_TOP);
    }
    else if (iarg == 11) {
	ed->ndice = &mob->mob_specials.hpnodice;
	ed->sizedice = &mob->mob_specials.hpsizedice;
	ed->diceextra32 = &mob->mob_specials.hpextra;
	ed->diceextra8 = NULL;
	olc_state_after_push(ed, OLC_STATE_MEDIT_TOP);
	olc_start_textedit(d, ed, "hitpoint dice", NULL, 1, 1);
    }
    else if (iarg == 12) {
	ed->ndice = &mob->mob_specials.damnodice;
	ed->sizedice = &mob->mob_specials.damsizedice;
	ed->diceextra8 = &GET_DAMROLL(mob);
	ed->diceextra32 = NULL;
	olc_state_after_push(ed, OLC_STATE_MEDIT_TOP);
	olc_start_textedit(d, ed, "hitpoint dice", NULL, 1, 1);
    }
    else if (iarg == 13) {
	olc_get_number(d, ed, "Gold (0-1000000): ", &GET_GOLD(mob),
		       sizeof(GET_GOLD(mob)), 0, 1000000, OLC_STATE_MEDIT_TOP);
    }
    else if (iarg == 14) {
	olc_get_number(d, ed, "Experience (0-1000000): ", &GET_EXP(mob),
		       sizeof(GET_EXP(mob)), 0, 1000000, OLC_STATE_MEDIT_TOP);
    }
    else if (iarg == 15) {
	olc_state_after_push(ed, OLC_STATE_MEDIT_TOP);
	olc_start_typeedit(d, ed, "load positon", &GET_POS(mob), position_types);
    }
    else if (iarg == 16) {
	olc_state_after_push(ed, OLC_STATE_MEDIT_TOP);
	olc_start_typeedit(d, ed, "default positon", &GET_DEFAULT_POS(mob), position_types);
    }
    else if (iarg == 17) {
	olc_state_after_push(ed, OLC_STATE_MEDIT_TOP);
	olc_start_typeedit(d, ed, "gender", &GET_SEX(mob), genders);
    }
    else if (iarg == 18) {
	olc_state_after_push(ed, OLC_STATE_MEDIT_TOP);
	olc_start_typeedit(d, ed, "attack types", &mob->mob_specials.attack_type, attack_types);
    }
    else if (iarg == 19) {
	olc_get_number(d, ed, "Strength (3-25): ", &mob->real_abils.str,
		       sizeof(mob->real_abils.str), 3, 25, OLC_STATE_MEDIT_TOP);
    }
    else if (iarg == 20) {
	olc_get_number(d, ed, "Strength Additonal (0-100): ", &mob->real_abils.str_add,
		       sizeof(mob->real_abils.str_add), 0, 100, OLC_STATE_MEDIT_TOP);
    }
    else if (iarg == 21) {
	olc_get_number(d, ed, "Intelligence (3-25): ", &mob->real_abils.intel,
		       sizeof(mob->real_abils.intel), 3, 25, OLC_STATE_MEDIT_TOP);
    }
    else if (iarg == 22) {
	olc_get_number(d, ed, "Wisdom (3-25): ", &mob->real_abils.wis,
		       sizeof(mob->real_abils.wis), 3, 25, OLC_STATE_MEDIT_TOP);
    }
    else if (iarg == 23) {
	olc_get_number(d, ed, "Dexterity (3-25): ", &mob->real_abils.dex,
		       sizeof(mob->real_abils.dex), 3, 25, OLC_STATE_MEDIT_TOP);
    }
    else if (iarg == 24) {
	olc_get_number(d, ed, "Constitution (3-25): ", &mob->real_abils.con,
		       sizeof(mob->real_abils.con), 3, 25, OLC_STATE_MEDIT_TOP);
    }
    else if (iarg == 25) {
	olc_get_number(d, ed, "Charisma (3-25): ", &mob->real_abils.cha,
		       sizeof(mob->real_abils.cha), 3, 25, OLC_STATE_MEDIT_TOP);
    }
    else {
	send_to_char(ch, "%s isn't a valid choice.\r\n", arg);
	olc_medit_display_top(d, ed);
    }
}

void olc_oedit_display_top(struct descriptor_data *d, struct olc_editor_s *ed)
{
    char buf[MAX_STRING_LENGTH];

    struct char_data *ch = d->character;
    int rnum = real_object(ed->vnum);
    struct obj_data *obj = &obj_proto[rnum];

    send_to_char(ch, "%sObject %d%s\r\n", CCCYN(ch, C_NRM), ed->vnum, CCNRM(ch, C_NRM));
    send_to_char(ch, "%s 1) Aliases: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 obj->name);
    send_to_char(ch, "%s 2) Short Description:%s %s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 obj->short_description);
    send_to_char(ch, "%s 3) Long Description:%s\r\n%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 obj->description);
    send_to_char(ch, "%s 4) Action Description:%s\r\n%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 obj->action_description);
    sprinttype(GET_OBJ_TYPE(obj), item_types, buf, sizeof(buf));
    send_to_char(ch, "%s 5) Item Type: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
    sprintbit(GET_OBJ_EXTRA(obj), extra_bits, buf, sizeof(buf));
    send_to_char(ch, "%s 6) Extra: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
    sprintbit(GET_OBJ_WEAR(obj), wear_bits, buf, sizeof(buf));
    send_to_char(ch, "%s 7) Wear: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
    send_to_char(ch, "%s 8) Weight: %s%d\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 GET_OBJ_WEIGHT(obj));
    send_to_char(ch, "%s 9) Cost: %s%d\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 GET_OBJ_COST(obj));
    send_to_char(ch, "%s10) Rent: %s%d\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 GET_OBJ_RENT(obj));

    if (GET_OBJ_TYPE(obj) == ITEM_LIGHT) {
	send_to_char(ch, "%s13) Capacity: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 2));
    }
    else if (GET_OBJ_TYPE(obj) == ITEM_SCROLL ||
	     GET_OBJ_TYPE(obj) == ITEM_POTION) {
	send_to_char(ch, "%s11) Caster Level: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 0));
	send_to_char(ch, "%s12) Spell 1: %s%s\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), skill_name(GET_OBJ_VAL(obj, 1)));
	send_to_char(ch, "%s13) Spell 2: %s%s\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), skill_name(GET_OBJ_VAL(obj, 2)));
	send_to_char(ch, "%s14) Spell 3: %s%s\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), skill_name(GET_OBJ_VAL(obj, 3)));
    }
    else if (GET_OBJ_TYPE(obj) == ITEM_WAND ||
	     GET_OBJ_TYPE(obj) == ITEM_STAFF) {
	send_to_char(ch, "%s11) Caster Level: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 0));
	send_to_char(ch, "%s12) Charge Capacity: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 1));
	send_to_char(ch, "%s13) Charges Remaining: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 2));
	send_to_char(ch, "%s14) Spell: %s%s\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), skill_name(GET_OBJ_VAL(obj, 3)));
    }
    else if (GET_OBJ_TYPE(obj) == ITEM_WEAPON) {
	send_to_char(ch, "%s12) Number Of Damage Dice: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 1));
	send_to_char(ch, "%s13) Size Of Damage Dice: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 2));
	sprinttype(GET_OBJ_VAL(obj, 3), attack_types, buf, sizeof(buf));
	send_to_char(ch, "%s14) Weapon Type: %s%s\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
    }
    else if (GET_OBJ_TYPE(obj) == ITEM_ARMOR) {
	send_to_char(ch, "%s11) AC Apply: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 0));
    }
    else if (GET_OBJ_TYPE(obj) == ITEM_CONTAINER) {
	send_to_char(ch, "%s11) Capacity: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 0));
	sprintbit(GET_OBJ_VAL(obj, 1), container_bits, buf, sizeof(buf));
	send_to_char(ch, "%s12) Flag bits: %s%s\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
	send_to_char(ch, "%s13) Key Number: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 2));
    }
    else if (GET_OBJ_TYPE(obj) == ITEM_DRINKCON ||
	     GET_OBJ_TYPE(obj) == ITEM_FOUNTAIN) {
	send_to_char(ch, "%s11) Capacity: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 0));
	send_to_char(ch, "%s12) Current Quantity: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 1));
	sprinttype(GET_OBJ_VAL(obj, 2), drinks, buf, sizeof(buf));
	send_to_char(ch, "%s13) Liquid Type: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
	send_to_char(ch, "%s14) Poisoned If Not Zero: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 3));
    }
    else if (GET_OBJ_TYPE(obj) == ITEM_FOOD) {
	send_to_char(ch, "%s11) Number Of Hours: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 0));
	send_to_char(ch, "%s14) Poisoned If Not Zero: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 3));
    }
    else if (GET_OBJ_TYPE(obj) == ITEM_MONEY) {
	send_to_char(ch, "%s11) Number Of Coins: %s%d\r\n",
		     CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), GET_OBJ_VAL(obj, 0));
    }

    // Extra descs
    struct extra_descr_data *extra = obj->ex_description;
    int option = 20;
    while (extra)
    {
	send_to_char(ch, "%s%d) Extra: %s%s\r\n",
		     CCCYN(ch, C_NRM), option++, CCNRM(ch, C_NRM), extra->keyword);

	extra = extra->next;
    }
    send_to_char(ch, "%s%d) Add new extra description%s\r\n",
		 CCCYN(ch, C_NRM), option++, CCNRM(ch, C_NRM));

    // Affects
    int new_affect_idx = -1;
    for (int i = 0; i < MAX_OBJ_AFFECT; i++)
    {
	if (obj->affected[i].location > 0)
	{
	    sprinttype(obj->affected[i].location, apply_types, buf, sizeof(buf));
	    send_to_char(ch, "%s%d) Affected: %s%-15s   %s%d) Amount: %s%d\r\n",
			 CCCYN(ch, C_NRM), i + 30, CCNRM(ch, C_NRM), buf,
			 CCCYN(ch, C_NRM), i + 40, CCNRM(ch, C_NRM), obj->affected[i].modifier);
	}
	else if (new_affect_idx < 0)
	    new_affect_idx = i;
    }
    send_to_char(ch, "%s%d) Affected: %s%-15s   %s%d) Amount: %s%d\r\n",
		 CCCYN(ch, C_NRM), new_affect_idx + 30, CCNRM(ch, C_NRM), "NEW",
		 CCCYN(ch, C_NRM), new_affect_idx + 40, CCNRM(ch, C_NRM),
		 obj->affected[new_affect_idx].modifier);

    send_to_char(ch, "\r\nEnter Choice (or . when done): ");
    ed->state =  OLC_STATE_OEDIT_TOPCHOICE;
}

void olc_oedit_handle_top(struct descriptor_data *d, struct olc_editor_s *ed, char *arg)
{
    struct char_data *ch = d->character;
    int rnum = real_object(ed->vnum);
    struct obj_data *obj = &obj_proto[rnum];

    if (!*arg)
    {
	send_to_char(ch, "Not a valid choice, try again: ");
	return;
    }

    int iarg = atoi(arg);

    if (*arg == '.') {
	olc_clear_editor(d->olc_editor_idx);
	STATE(d) = CON_PLAYING;
	REMOVE_BIT(PLR_FLAGS(ch), PLR_WRITING);
    }
    else if (iarg == 1) {
	olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
	olc_start_textedit(d, ed, "name", &obj->name, 1, 0);
    }
    else if (iarg == 2) {
	olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
	olc_start_textedit(d, ed, "short desc", &obj->short_description, 1, 0);
    }
    else if (iarg == 3) {
	olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
	olc_start_textedit(d, ed, "long desc", &obj->description, 0, 0);
    }
    else if (iarg == 4) {
	olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
	olc_start_textedit(d, ed, "action desc", &obj->action_description, 0, 0);
    }
    else if (iarg == 5) {
	olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
	olc_start_typeedit(d, ed, "item type", &GET_OBJ_TYPE(obj), item_types);
    }
    else if (iarg == 6) {
	olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
	olc_start_toggleedit(d, ed, "extra", &GET_OBJ_EXTRA(obj), extra_bits);
    }
    else if (iarg == 7) {
	olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
	olc_start_toggleedit(d, ed, "wear", &GET_OBJ_WEAR(obj), wear_bits);
    }
    else if (iarg == 8) {
	olc_get_number(d, ed, "Weight (1-255): ", &GET_OBJ_WEIGHT(obj),
		       sizeof(GET_OBJ_WEIGHT(obj)), 0, 255, OLC_STATE_OEDIT_TOP);
    }
    else if (iarg == 9) {
	olc_get_number(d, ed, "Cost (1-1000000): ", &GET_OBJ_COST(obj),
		       sizeof(GET_OBJ_COST(obj)), 0, 1000000, OLC_STATE_OEDIT_TOP);
    }
    else if (iarg == 10) {
	olc_get_number(d, ed, "Rent (1-1000000): ", &GET_OBJ_RENT(obj),
		       sizeof(GET_OBJ_RENT(obj)), 0, 1000000, OLC_STATE_OEDIT_TOP);
    }
    else if (iarg >= 11 && iarg <= 14)
    {
	if (GET_OBJ_TYPE(obj) == ITEM_LIGHT) {
	    if (iarg == 13) {
		olc_get_number(d, ed, "Capacity (1-1000000): ", &GET_OBJ_VAL(obj, 2),
			       sizeof(GET_OBJ_VAL(obj, 2)), 0, 1000000, OLC_STATE_OEDIT_TOP);
	    }
	    else
		goto invalid_choice;
	}
	else if (GET_OBJ_TYPE(obj) == ITEM_SCROLL ||
		 GET_OBJ_TYPE(obj) == ITEM_POTION) {
	    if (iarg == 11) {
		olc_get_number(d, ed, "Caster Level (1-30): ", &GET_OBJ_VAL(obj, 0),
			       sizeof(GET_OBJ_VAL(obj, 0)), 0, 30, OLC_STATE_OEDIT_TOP);
	    }
	    else if (iarg == 12)
		olc_start_getspellname(d, ed, &GET_OBJ_VAL(obj, 1));
	    else if (iarg == 13)
		olc_start_getspellname(d, ed, &GET_OBJ_VAL(obj, 2));
	    else if (iarg == 14)
		olc_start_getspellname(d, ed, &GET_OBJ_VAL(obj, 3));
	    else
		goto invalid_choice;
	}
	else if (GET_OBJ_TYPE(obj) == ITEM_WAND ||
		 GET_OBJ_TYPE(obj) == ITEM_STAFF) {
	    if (iarg == 11) {
		olc_get_number(d, ed, "Caster Level (1-30): ", &GET_OBJ_VAL(obj, 0),
			       sizeof(GET_OBJ_VAL(obj, 0)), 0, 30, OLC_STATE_OEDIT_TOP);
	    }
	    else if (iarg == 12) {
		olc_get_number(d, ed, "Charge Capacity (1-1000000): ", &GET_OBJ_VAL(obj, 1),
			       sizeof(GET_OBJ_VAL(obj, 1)), 0, 1000000, OLC_STATE_OEDIT_TOP);
	    }
	    else if (iarg == 13) {
		olc_get_number(d, ed, "Charges Remaining (1-1000000): ", &GET_OBJ_VAL(obj, 2),
			       sizeof(GET_OBJ_VAL(obj, 2)), 0, 1000000, OLC_STATE_OEDIT_TOP);
	    }
	    else if (iarg == 14)
		olc_start_getspellname(d, ed, &GET_OBJ_VAL(obj, 3));
	    else
		goto invalid_choice;
	}
	else if (GET_OBJ_TYPE(obj) == ITEM_WEAPON) {
	    if (iarg == 12) {
		olc_get_number(d, ed, "Number Of Damage Dice (1-255): ", &GET_OBJ_VAL(obj, 1),
			       sizeof(GET_OBJ_VAL(obj, 1)), 0, 255, OLC_STATE_OEDIT_TOP);
	    }
	    else if (iarg == 13) {
		olc_get_number(d, ed, "Size Of Damage Dice (1-255): ", &GET_OBJ_VAL(obj, 2),
			       sizeof(GET_OBJ_VAL(obj, 2)), 0, 255, OLC_STATE_OEDIT_TOP);
	    }
	    else if (iarg == 14) {
		olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
		olc_start_typeedit(d, ed, "weapon type", &GET_OBJ_VAL(obj, 3), attack_types);
	    }
	    else
		goto invalid_choice;
	}
	else if (GET_OBJ_TYPE(obj) == ITEM_ARMOR) {
	    if (iarg == 11) {
		olc_get_number(d, ed, "AC Apply (-10-10): ", &GET_OBJ_VAL(obj, 0),
			       sizeof(GET_OBJ_VAL(obj, 0)), -10, 10, OLC_STATE_OEDIT_TOP);
	    }
	    else
		goto invalid_choice;
	}
	else if (GET_OBJ_TYPE(obj) == ITEM_CONTAINER) {
	    if (iarg == 11) {
		olc_get_number(d, ed, "Capacity (1-1000000): ", &GET_OBJ_VAL(obj, 0),
			       sizeof(GET_OBJ_VAL(obj, 0)), 1, 1000000, OLC_STATE_OEDIT_TOP);
	    }
	    else if (iarg == 12) {
		olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
		olc_start_toggleedit(d, ed, "container flags", &GET_OBJ_VAL(obj, 1),
				     container_bits);
	    }
	    else if (iarg == 13) {
		olc_get_number(d, ed, "Key Number (0-100000): ", &GET_OBJ_VAL(obj, 2),
			       sizeof(GET_OBJ_VAL(obj, 2)), 0, 100000, OLC_STATE_OEDIT_TOP);
	    }
	    else
		goto invalid_choice;
	}
	else if (GET_OBJ_TYPE(obj) == ITEM_DRINKCON ||
		 GET_OBJ_TYPE(obj) == ITEM_FOUNTAIN) {
	    if (iarg == 11) {
		olc_get_number(d, ed, "Capacity (1-1000000): ", &GET_OBJ_VAL(obj, 0),
			       sizeof(GET_OBJ_VAL(obj, 0)), 1, 1000000, OLC_STATE_OEDIT_TOP);
	    }
	    else if (iarg == 12) {
		olc_get_number(d, ed, "Current Quantity (1-1000000): ", &GET_OBJ_VAL(obj, 1),
			       sizeof(GET_OBJ_VAL(obj, 1)), 1, 1000000, OLC_STATE_OEDIT_TOP);
	    }
	    else if (iarg == 13) {
		olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
		olc_start_typeedit(d, ed, "liquid type", &GET_OBJ_VAL(obj, 2), drinks);
	    }
	    else if (iarg == 14) {
		olc_get_number(d, ed, "Poisoned (0-1): ", &GET_OBJ_VAL(obj, 3),
			       sizeof(GET_OBJ_VAL(obj, 3)), 0, 1, OLC_STATE_OEDIT_TOP);
	    }
	    else
		goto invalid_choice;
	}
	else if (GET_OBJ_TYPE(obj) == ITEM_FOOD) {
	    if (iarg == 11) {
		olc_get_number(d, ed, "Hours Of Sustenance (1-100): ", &GET_OBJ_VAL(obj, 0),
			       sizeof(GET_OBJ_VAL(obj, 0)), 1, 100, OLC_STATE_OEDIT_TOP);
	    }
	    else if (iarg == 14) {
		olc_get_number(d, ed, "Poisoned (0-1): ", &GET_OBJ_VAL(obj, 3),
			       sizeof(GET_OBJ_VAL(obj, 3)), 0, 1, OLC_STATE_OEDIT_TOP);
	    }
	    else
		goto invalid_choice;
	}
	else if (GET_OBJ_TYPE(obj) == ITEM_MONEY) {
	    if (iarg == 11) {
		olc_get_number(d, ed, "Number Of Coins (1-1000000): ", &GET_OBJ_VAL(obj, 0),
			       sizeof(GET_OBJ_VAL(obj, 0)), 1, 1000000, OLC_STATE_OEDIT_TOP);
	    }
	    else
		goto invalid_choice;
	}
	else
	    goto invalid_choice;
    }
    else if (iarg >= 20 && iarg <30)
    {
	struct extra_descr_data *extra = obj->ex_description;
	int i = 20;
	while (extra != NULL)
	{
	    if (i == iarg)
	    {
		printf("Edit extra desc %d - %s\n", i, extra->keyword);
		ed->extra_desc_list = &obj->ex_description;
		ed->extra_desc = extra;
		olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
		ed->state = OLC_STATE_EXTRADESC_TOP;
		olc_nanny(d, "");
		return;
	    }
	    extra = extra->next;
	    i++;
	}

	if (i == iarg)
	{
	    printf("Edit new extra desc %d\n", i);
	    ed->extra_desc_list = &obj->ex_description;
	    ed->extra_desc = NULL;
	    olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
	    ed->state = OLC_STATE_EXTRADESC_TOP;
	    olc_nanny(d, "");
	    return;
	}
	goto invalid_choice;
    }
    else if (iarg >= 30 && iarg < 30 + MAX_OBJ_AFFECT)
    {
	int i = iarg - 30;
	olc_state_after_push(ed, OLC_STATE_OEDIT_TOP);
	olc_start_typeedit8(d, ed, "affected", &obj->affected[i].location, apply_types);
    }
    else if (iarg >= 40 && iarg < 40 + MAX_OBJ_AFFECT)
    {
	int i = iarg - 40;
	olc_get_number(d, ed, "Affect Amount (-100-100): ",
		       &obj->affected[i].modifier,
		       sizeof(obj->affected[i].modifier),
		       -100, 100, OLC_STATE_OEDIT_TOP);
    }
    else {
	send_to_char(ch, "%s isn't a valid choice.\r\n", arg);
	olc_oedit_display_top(d, ed);
    }

    return;
invalid_choice:
    send_to_char(ch, "%s isn't a valid choice.\r\n", arg);
    olc_oedit_display_top(d, ed);
}

void olc_redit_display_top(struct descriptor_data *d, struct olc_editor_s *ed)
{
    char buf[MAX_STRING_LENGTH];

    struct char_data *ch = d->character;
    int room_rnum = real_room(ed->vnum);
    struct room_data *room = &world[room_rnum];

    send_to_char(ch, "%sRoom %d%s\r\n", CCCYN(ch, C_NRM), ed->vnum, CCNRM(ch, C_NRM));
    send_to_char(ch, "%s 1) Name: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), room->name);
    send_to_char(ch, "%s 2) Description:%s\r\n%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM),
		 room->description);
    sprintbit(room->room_flags, room_bits, buf, sizeof(buf));
    send_to_char(ch, "%s 3) Flags: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);
    sprinttype(room->sector_type, sector_types, buf, sizeof(buf));
    send_to_char(ch, "%s 4) Sector Type: %s%s\r\n", CCCYN(ch, C_NRM), CCNRM(ch, C_NRM), buf);

    send_to_char(ch, "%sExits:\r\n", CCCYN(ch, C_NRM));
    for (int i = 0; i < NUM_OF_DIRS; i++)
    {
	struct room_direction_data *exit = room->dir_option[i];

	if (exit == NULL)
	{
	    send_to_char(ch, "  %c) %sNO EXIT%s\r\n", directions[i],
			 CCNRM(ch, C_NRM), CCCYN(ch, C_NRM));
	}
	else if (exit->to_room == NOWHERE)
	{
	    send_to_char(ch, "  %c) %sNOWHERE%s\r\n", directions[i],
			 CCNRM(ch, C_NRM), CCCYN(ch, C_NRM));
	}
	else
	{
	    send_to_char(ch, "  %c) %s%d - %s%s\r\n", directions[i],
			 CCNRM(ch, C_NRM), world[exit->to_room].number,
			 world[exit->to_room].name, CCCYN(ch, C_NRM));
	}
    }

    struct extra_descr_data *extra = room->ex_description;
    int option = 5;
    while (extra)
    {
	send_to_char(ch, "%s %d) Extra: %s%s\r\n",
		     CCCYN(ch, C_NRM), option++, CCNRM(ch, C_NRM), extra->keyword);

	extra = extra->next;
    }
    send_to_char(ch, "%s %d) Add new extra description%s\r\n",
		 CCCYN(ch, C_NRM), option++, CCNRM(ch, C_NRM));

    send_to_char(ch, "\r\nEnter Choice (or . when done): ");
    ed->state =  OLC_STATE_REDIT_TOPCHOICE;
}

void olc_redit_handle_top(struct descriptor_data *d, struct olc_editor_s *ed, char *arg)
{
    struct char_data *ch = d->character;
    int room_rnum = real_room(ed->vnum);
    struct room_data *room = &world[room_rnum];

    if (!*arg)
    {
	send_to_char(ch, "Not a valid choice, try again: ");
	return;
    }

    int iarg = atoi(arg);
    if (iarg >= 5)
    {
	struct extra_descr_data *extra = room->ex_description;
	int i = 5;
	while (extra != NULL)
	{
	    if (i == iarg)
	    {
		printf("Edit extra desc %d - %s\n", i, extra->keyword);
		ed->extra_desc_list = &room->ex_description;
		ed->extra_desc = extra;
		olc_state_after_push(ed, OLC_STATE_REDIT_TOP);
		ed->state = OLC_STATE_EXTRADESC_TOP;
		olc_nanny(d, "");
		return;
	    }
	    extra = extra->next;
	    i++;
	}

	if (i == iarg)
	{
	    printf("Edit new extra desc %d\n", i);
	    ed->extra_desc_list = &room->ex_description;
	    ed->extra_desc = NULL;
	    olc_state_after_push(ed, OLC_STATE_REDIT_TOP);
	    ed->state = OLC_STATE_EXTRADESC_TOP;
	    olc_nanny(d, "");
	}

	return;
    }

    switch (*arg)
    {
      case '1':
	olc_state_after_push(ed, OLC_STATE_REDIT_TOP);
	olc_start_textedit(d, ed, "name", &room->name, 1, 0);
	break;
      case '2':
	olc_state_after_push(ed, OLC_STATE_REDIT_TOP);
	olc_start_textedit(d, ed, "description", &room->description, 0, 0);
	break;
      case '3':
	olc_state_after_push(ed, OLC_STATE_REDIT_TOP);
	olc_start_toggleedit(d, ed, "room flags", &room->room_flags, room_bits);
	break;
      case '4':
	olc_state_after_push(ed, OLC_STATE_REDIT_TOP);
	olc_start_typeedit(d, ed, "sector type", &room->sector_type, sector_types);
	break;
      case 'N':
      case 'n':
	olc_direction_display_top(d, ed, 0);
	break;
      case 'E':
      case 'e':
	olc_direction_display_top(d, ed, 1);
	break;
      case 'S':
      case 's':
	olc_direction_display_top(d, ed, 2);
	break;
      case 'W':
      case 'w':
	olc_direction_display_top(d, ed, 3);
	break;
      case 'U':
      case 'u':
	olc_direction_display_top(d, ed, 4);
	break;
      case 'D':
      case 'd':
	olc_direction_display_top(d, ed, 5);
	break;
      case '.':
	olc_clear_editor(d->olc_editor_idx);
	STATE(d) = CON_PLAYING;
	REMOVE_BIT(PLR_FLAGS(ch), PLR_WRITING);
	break;
      default:
	send_to_char(ch, "%c isn't a valid choice.\r\n", *arg);
	olc_redit_display_top(d, ed);
	break;
    }
}

void olc_nanny(struct descriptor_data *d, char *arg)
{
    if (d->olc_editor_idx <= 0 && d->olc_editor_idx >= MAX_EDITORS)
    {
	STATE(d) = CON_CLOSE;
	REMOVE_BIT(PLR_FLAGS(d->character), PLR_WRITING);
	return;
    }

    struct olc_editor_s *ed = &olc_editors[d->olc_editor_idx];

    if (GET_IDNUM(d->character) != ed->idnum)
    {
	if (ed->idnum != 0)
	{
	    ed->idnum = 0;
	    olc_clear_editor(d->olc_editor_idx);
	}
	STATE(d) = CON_CLOSE;
	REMOVE_BIT(PLR_FLAGS(d->character), PLR_WRITING);
	return;
    }

    switch (ed->state)
    {
      case OLC_STATE_MEDIT_TOP:
	olc_medit_display_top(d, ed);
	break;

      case OLC_STATE_MEDIT_TOPCHOICE:
	olc_medit_handle_top(d, ed, arg);
	break;

      case OLC_STATE_OEDIT_TOP:
	olc_oedit_display_top(d, ed);
	break;

      case OLC_STATE_OEDIT_TOPCHOICE:
	olc_oedit_handle_top(d, ed, arg);
	break;

      case OLC_STATE_REDIT_TOP:
	olc_redit_display_top(d, ed);
	break;

      case OLC_STATE_REDIT_TOPCHOICE:
	olc_redit_handle_top(d, ed, arg);
	break;

      case OLC_STATE_DIRECTION_TOP:
	olc_direction_display_top(d, ed, ed->direction);
	break;

      case OLC_STATE_DIRECTION_TOPCHOICE:
	olc_direction_handle_top(d, ed, arg);
	break;

      case OLC_STATE_TEXTEDIT:
	olc_handle_textedit(d, ed, arg);
	break;

      case OLC_STATE_TOGGLEEDIT:
	olc_handle_toggleedit(d, ed, arg);
	break;

      case OLC_STATE_TYPEEDIT:
	olc_handle_typeedit(d, ed, arg);
	break;

      case OLC_STATE_DOOR_TO_ROOM:
	int room_rnum = real_room(ed->vnum);
	struct room_data *room = &world[room_rnum];
	struct room_direction_data *exit = room->dir_option[ed->direction];
	int to_vnum = atoi(arg);
	int to_rnum = real_room(to_vnum);
	exit->to_room = to_rnum;
	olc_direction_display_top(d, ed, ed->direction);
	break;

      case OLC_STATE_DOOR_KEY_NUMBER:
	room_rnum = real_room(ed->vnum);
	room = &world[room_rnum];
	exit = room->dir_option[ed->direction];
	exit->key = atoi(arg);
	olc_direction_display_top(d, ed, ed->direction);
	break;

      case OLC_STATE_EXTRADESC_TOP:
	olc_extradesc_display_top(d, ed);
	break;

      case OLC_STATE_EXTRADESC_TOPCHOICE:
	olc_extradesc_handle_top(d, ed, arg);
	break;

      case OLC_STATE_NUMBER:
	int n = atoi(arg);
	if (n >= ed->number_min && n <= ed->number_max)
	{
	    switch (ed->number_field_size)
	    {
	      case 1:
		char *b = ed->number_field;
		*b = n;
		break;
	      case 2:
		short *s = ed->number_field;
		*s = n;
		break;
	      case 4:
		int *i = ed->number_field;
		*i = n;
		break;
	    }
	}

	ed->number_field = NULL;
	ed->state = olc_state_after_pop(ed);
	olc_nanny(d, "");
	break;
    }
}

void do_redit(struct char_data *ch, char *argument, int cmd, int subcmd)
{
    skip_spaces(&argument);

    if (!*argument)
    {
	send_to_char(ch, "You need to specify a room number.\r\n");
	return;
    }

    int room_vnum = atoi(argument);
    int room_rnum = real_room(room_vnum);
    if (room_rnum == NOWHERE)
    {
	send_to_char(ch, "Room %d doesn't exist.\r\n", room_vnum);
	return;
    }

    if (!olc_ok_to_edit(ch, room_vnum))
    {
	send_to_char(ch, "You don't have permission to edit that zone.\r\n");
	return;
    }

    olc_create_editor(ch);

    if (ch->desc->olc_editor_idx > 0)
    {
	olc_editors[ch->desc->olc_editor_idx].garbage_list = NULL;
	olc_editors[ch->desc->olc_editor_idx].state = OLC_STATE_REDIT_TOP;
	olc_editors[ch->desc->olc_editor_idx].vnum = room_vnum;
	olc_editors[ch->desc->olc_editor_idx].edit_type = OLC_EDIT_ROOM;
	olc_nanny(ch->desc, "");
	STATE(ch->desc) = CON_OLC_EDIT;
	SET_BIT(PLR_FLAGS(ch), PLR_WRITING);
    }
}

void do_medit(struct char_data *ch, char *argument, int cmd, int subcmd)
{
    skip_spaces(&argument);

    if (!*argument)
    {
	send_to_char(ch, "You need to specify a mobile number.\r\n");
	return;
    }

    int vnum = atoi(argument);
    int rnum = real_mobile(vnum);
    if (rnum == NOBODY)
    {
	send_to_char(ch, "Mobile %d doesn't exist.\r\n", vnum);
	return;
    }

    if (!olc_ok_to_edit(ch, vnum))
    {
	send_to_char(ch, "You don't have permission to edit that zone.\r\n");
	return;
    }

    olc_create_editor(ch);

    if (ch->desc->olc_editor_idx > 0)
    {
	olc_editors[ch->desc->olc_editor_idx].garbage_list = &olc_mob_garbage;
	olc_editors[ch->desc->olc_editor_idx].state = OLC_STATE_MEDIT_TOP;
	olc_editors[ch->desc->olc_editor_idx].vnum = vnum;
	olc_editors[ch->desc->olc_editor_idx].edit_type = OLC_EDIT_MOBILE;
	olc_nanny(ch->desc, "");
	STATE(ch->desc) = CON_OLC_EDIT;
	SET_BIT(PLR_FLAGS(ch), PLR_WRITING);
    }
}

void do_oedit(struct char_data *ch, char *argument, int cmd, int subcmd)
{
    skip_spaces(&argument);

    if (!*argument)
    {
	send_to_char(ch, "You need to specify a object number.\r\n");
	return;
    }

    int vnum = atoi(argument);
    int rnum = real_object(vnum);
    if (rnum == NOBODY)
    {
	send_to_char(ch, "Object %d doesn't exist.\r\n", vnum);
	return;
    }

    if (!olc_ok_to_edit(ch, vnum))
    {
	send_to_char(ch, "You don't have permission to edit that zone.\r\n");
	return;
    }

    olc_create_editor(ch);

    if (ch->desc->olc_editor_idx > 0)
    {
	olc_editors[ch->desc->olc_editor_idx].garbage_list = &olc_obj_garbage;
	olc_editors[ch->desc->olc_editor_idx].state = OLC_STATE_OEDIT_TOP;
	olc_editors[ch->desc->olc_editor_idx].vnum = vnum;
	olc_editors[ch->desc->olc_editor_idx].edit_type = OLC_EDIT_OBJECT;
	olc_nanny(ch->desc, "");
	STATE(ch->desc) = CON_OLC_EDIT;
	SET_BIT(PLR_FLAGS(ch), PLR_WRITING);
    }
}

static char *olc_bits_to_letters(int bits, char *buf)
{
    memset(buf, 0, 33);

    if (bits == 0)
    {
	buf[0] = '0';
	return buf;
    }

    int pos = 0;
    for (int i = 0; i < 32; i++)
	if (bits & (1 << i))
	    buf[pos++] = 'a' + i;

    return buf;
}

static void olc_save_mobile(int vnum)
{
    int rnum = real_mobile(vnum);
    if (rnum == NOBODY)
	return;

    struct char_data *mob = &mob_proto[rnum];

    FILE *fp = fopen("world/mob/medit.mob", "a");
    if (fp == NULL)
	return;

    char mob_type = 'E';
    if (mob->real_abils.str == 11 && mob->real_abils.intel == 11 && mob->real_abils.wis == 11 &&
	mob->real_abils.dex == 11 && mob->real_abils.con == 11 && mob->real_abils.cha == 11 &&
	mob->mob_specials.attack_type == 0)
    {
	mob_type = 'S';
    }

    fprintf(fp, "#%d\n", vnum);
    fprintf(fp, "%s~\n", mob->player.name);
    fprintf(fp, "%s~\n", mob->player.short_descr);
    fprintf(fp, "%s~\n", mob->player.long_descr);
    fprintf(fp, "%s~\n", mob->player.description);

    char action_flags[33];
    char affect_flags[33];
    fprintf(fp, "%s %s %d %c\n",
	    olc_bits_to_letters(MOB_FLAGS(mob) & ~MOB_ISNPC, action_flags),
	    olc_bits_to_letters(AFF_FLAGS(mob), affect_flags), GET_ALIGNMENT(mob), mob_type);
    fprintf(fp, "%d %d %d %dd%d+%d %dd%d+%d\n",
	    GET_LEVEL(mob), 20 - GET_HITROLL(mob), GET_AC(mob) / 10,
	    mob->mob_specials.hpnodice, mob->mob_specials.hpsizedice, mob->mob_specials.hpextra,
	    mob->mob_specials.damnodice, mob->mob_specials.damsizedice, GET_DAMROLL(mob));
    fprintf(fp, "%d %d\n", GET_GOLD(mob), GET_EXP(mob));
    fprintf(fp, "%d %d %d\n", GET_POS(mob), GET_DEFAULT_POS(mob), GET_SEX(mob));

    if (mob_type == 'E')
    {
	if (mob->mob_specials.attack_type > 0)
	    fprintf(fp, "BareHandAttack: %d\n", mob->mob_specials.attack_type);
	if (mob->real_abils.str != 11)
	    fprintf(fp, "Str: %d\n", mob->real_abils.str);
	if (mob->real_abils.intel != 11)
	    fprintf(fp, "Int: %d\n", mob->real_abils.intel);
	if (mob->real_abils.wis != 11)
	    fprintf(fp, "Wis: %d\n", mob->real_abils.wis);
	if (mob->real_abils.dex != 11)
	    fprintf(fp, "Dex: %d\n", mob->real_abils.dex);
	if (mob->real_abils.con != 11)
	    fprintf(fp, "Con: %d\n", mob->real_abils.con);
	if (mob->real_abils.cha != 11)
	    fprintf(fp, "Cha: %d\n", mob->real_abils.cha);
	if (mob->real_abils.str == 18)
	    fprintf(fp, "StrAdd: %d\n", mob->real_abils.str_add);
	fprintf(fp, "E\n");
    }

    fclose(fp);
}

static void olc_save_object(int vnum)
{
    int rnum = real_object(vnum);
    if (rnum == NOTHING)
	return;

    struct obj_data *obj = &obj_proto[rnum];

    FILE *fp = fopen("world/obj/oedit.obj", "a");
    if (fp == NULL)
	return;

    fprintf(fp, "#%d\n", vnum);
    fprintf(fp, "%s~\n", obj->name);
    fprintf(fp, "%s~\n", obj->short_description);
    fprintf(fp, "%s~\n", obj->description != NULL ? obj->description : "");
    fprintf(fp, "%s~\n", obj->action_description != NULL ? obj->action_description : "");

    char extra_flags[33];
    char wear_flags[33];
    fprintf(fp, "%d %s %s\n", GET_OBJ_TYPE(obj),
	    olc_bits_to_letters(GET_OBJ_EXTRA(obj), extra_flags),
	    olc_bits_to_letters(GET_OBJ_WEAR(obj), wear_flags));
    fprintf(fp, "%d %d %d %d\n",
	    GET_OBJ_VAL(obj, 0), GET_OBJ_VAL(obj, 1), GET_OBJ_VAL(obj, 2), GET_OBJ_VAL(obj, 3));
    fprintf(fp, "%d %d %d\n", GET_OBJ_WEIGHT(obj), GET_OBJ_COST(obj), GET_OBJ_RENT(obj));

    for (int i = 0; i < MAX_OBJ_AFFECT; i++)
    {
	if (obj->affected[i].location > 0)
	    fprintf(fp, "A\n%d %d\n", obj->affected[i].location, obj->affected[i].modifier);
    }

    struct extra_descr_data *extra = obj->ex_description;
    while (extra)
    {
	fprintf(fp, "E\n%s~\n%s~\n", extra->keyword, extra->description);
	extra = extra->next;
    }

    fclose(fp);
}

static void olc_save_room(int vnum)
{
    int rnum = real_room(vnum);
    if (rnum == NOWHERE)
	return;

    struct room_data *room = &world[rnum];

    FILE *fp = fopen("world/wld/redit.wld", "a");
    if (fp == NULL)
	return;

    fprintf(fp, "#%d\n", vnum);
    fprintf(fp, "%s~\n", room->name);
    fprintf(fp, "%s~\n", room->description);

    char room_flags[33];
    fprintf(fp, "%d %s %d\n", zone_table[room->zone].number,
	    olc_bits_to_letters(room->room_flags, room_flags), room->sector_type);

    for (int i = 0; i < NUM_OF_DIRS; i++)
    {
	struct room_direction_data *exit = room->dir_option[i];

	if (exit != NULL && exit->to_room != NOWHERE)
	{
	    fprintf(fp, "D%d\n%s~\n%s~\n", i,
		    exit->general_description == NULL ? "" : exit->general_description,
		    exit->keyword == NULL ? "" : exit->keyword);

	    fprintf(fp, "%d %d %d\n", exit->exit_info, exit->key, world[exit->to_room].number);
	}
    }


    struct extra_descr_data *extra = room->ex_description;
    while (extra)
    {
	fprintf(fp, "E\n%s~\n%s~\n", extra->keyword, extra->description);
	extra = extra->next;
    }

    fprintf(fp, "S\n");
    fclose(fp);
}

/*
 * Zone editing:
 *
 * open, closed
 * locked, unlocked
 *
 * Nobody can rent items from a closed zone. Mortals can't enter a closed zone.  IMMs
 * can only enter a closed zone if they are an author or editor.
 *
 * Locked prevents mob from wandering and is the primary mode for adding/removing
 * mobiles and objects from a zone.  When zone is unlocked.  Current layout is saved
 * to the zone file.
 *
 * ZEDIT <zone> <command from list below>
 * OPEN/CLOSE
 * LOCK/UNLOCK
 * MOBILE - load mobile
 * OBJECT - load object on ground
 * GIVE   - load object and put in mobile inventory
 * EQUIP  - load object and have mobile wear/hold/wield it.
 * PURGE  - same as PURGE cmd, but permissions based on OLC system to use it.
 * CREATE - create new zone
 * GRANT <author/editor> <player>
 * REVOKE <author/editor> <player>
 */
void zedit_create(struct char_data *ch, int zone_num, char *argument)
{
    int rnum = top_of_zone_table + 1;
    if (rnum >= num_allocated_zone)
    {
	send_to_char(ch, "No more zones available.  Reboot to get more zones.\r\n");
	return;
    }

    zone_table[rnum].number = zone_num;
    zone_table[rnum].reset_mode = ZONE_RESETMODE_NOPCSINZONE;
    zone_table[rnum].name = strdup(argument);
    zone_table[rnum].bot = zone_num * 100;
    zone_table[rnum].top = zone_num * 100 + 99;
    zone_table[rnum].lifespan = 20;
    zone_table[rnum].permissions.flags = OLC_ZONEFLAGS_CLOSED;

    CREATE(zone_table[rnum].cmd, struct reset_com, 1);
    zone_table[rnum].cmd[0].command = 'S';

    top_of_zone_table++;
    mudlog(NRM, GET_LEVEL(ch), TRUE, "%s created zone %d (%d) - '%s'",
	   GET_NAME(ch), zone_num, rnum, argument);
}

void do_zedit(struct char_data *ch, char *argument, int cmd, int subcmd)
{
    int zone;
    int nconsumed;
    char *s1 = NULL;

    int nfields = sscanf(argument, "%d %ms %n", &zone, &s1, &nconsumed);
    if (nfields < 2)
    {
	send_to_char(ch, "ZEDIT <zone> <command> ...\r\n");
	return;
    }

    int rnum = real_zone(zone);
    if (strncmp("create", s1, strlen(s1)) == 0)
    {
	if (rnum != NOWHERE)
	{
	    send_to_char(ch, "Zone %d already exists\r\n", zone);
	    return;
	}

	zedit_create(ch, zone, argument + nconsumed);
	return;
    }

    if (rnum == NOWHERE)
    {
	send_to_char(ch, "Zone %d doesn't exist\r\n", zone);
	return;
    }
}

int olc_vnum_to_zone_rnum(int vnum)
{
    for (int i = 0; i <= top_of_zone_table; i++)
	if (zone_table[i].bot <= vnum && zone_table[i].top >= vnum)
	    return i;
    return NOWHERE;
}

int olc_ok_to_edit(struct char_data *ch, int vnum)
{
    if (!IS_NPC(ch) && GET_LEVEL(ch) >= LVL_GRGOD)
	return 1;

    int rnum = olc_vnum_to_zone_rnum(vnum);
    if (rnum == NOWHERE)
	return 0;

    struct zone_data *z = &zone_table[rnum];
    struct olc_permissions_s *perm = &z->permissions;

    if ((perm->flags & OLC_ZONEFLAGS_CLOSED) == 0)
	return 0;

    for (int i = 0; i < OLC_ZONE_MAX_AUTHORS; i++)
    {
	if (perm->authors[i] == GET_PFILEPOS(ch) || perm->editors[i] == GET_PFILEPOS(ch))
	    return 1;
    }

    return 0;
}

int olc_ok_to_enter(struct char_data *ch, struct room_data *rm)
{
    if (IS_NPC(ch) || GET_LEVEL(ch) >= LVL_IMMORT)
	return 1;

    if ((zone_table[rm->zone].permissions.flags & OLC_ZONEFLAGS_CLOSED))
	return 0;

    return 1;
}

int olc_ok_to_use_or_rent(struct char_data *ch, int vnum)
{
    if (!IS_NPC(ch) && GET_LEVEL(ch) < LVL_GRGOD)
    {
	int rnum = olc_vnum_to_zone_rnum(vnum);
	if ((zone_table[rnum].permissions.flags & OLC_ZONEFLAGS_CLOSED))
	    return 0;
    }

    return 1;
}

void olc_load_permissions(void)
{
    FILE *fp = fopen("world/permission.dat", "r");
    if (fp == NULL)
	return;

    for (int i = 0; i <= top_of_zone_table; i++)
    {
	int vnum = zone_table[i].number;
	if (fseek(fp, vnum * sizeof(struct olc_permissions_s), SEEK_SET) == 0)
	{
	    int n = fread(&zone_table[i].permissions, sizeof(struct olc_permissions_s), 1, fp);
	    n = n;
	}
    }
}

void olc_save_permissions(int vnum)
{
    FILE *fp = fopen("world/permission.dat", "r+");
    if (fp == NULL)
	return;

    int rnum = real_zone(vnum);
    if (rnum == NOWHERE)
	return;

    if (fseek(fp, vnum * sizeof(struct olc_permissions_s), SEEK_SET) == 0)
    {
	fwrite(&zone_table[rnum].permissions, sizeof(struct olc_permissions_s), 1, fp);
    }
}
