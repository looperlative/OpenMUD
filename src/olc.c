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
#include "olc.h"

struct olc_editor_s olc_editors[MAX_EDITORS];

void olc_clear_editor(int idx)
{
    struct olc_editor_s *ed = &olc_editors[idx];

    // Should free any allocated memory held by this editor.

    // Clear the editor.
    memset(ed, 0, sizeof(*ed));
}

void olc_create_editor(struct char_data *ch)
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
	ed->state = ed->state_after;
	olc_nanny(d, "");
	return;
    }
    else
    {
	int n = atoi(arg);
	if (n >= 1 && n <= ed->n_bits && ed->bit_names[n-1][0] != '*')
	    *ed->flags_field ^= (1 << (n - 1));
    }

    sprintbit(*ed->flags_field, ed->bit_names, buf, sizeof(buf));
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
	ed->bit_names = NULL;
	ed->n_bits = 0;
	ed->field_name = NULL;
	ed->state = ed->state_after;
	olc_nanny(d, "");
	return;
    }
    else
    {
	int n = atoi(arg);
	if (n >= 1 && n <= ed->n_bits && ed->bit_names[n-1][0] != '*')
	    *ed->flags_field = n - 1;
    }

    sprinttype(*ed->flags_field, ed->bit_names, buf, sizeof(buf));
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
    ed->state = OLC_STATE_TYPEEDIT;
    ed->n_bits = 0;

    olc_handle_typeedit(d, ed, "");
}

void olc_start_textedit(struct descriptor_data *d, struct olc_editor_s *ed,
			char *field_name, char **string, int is_single_line)
{
    struct char_data *ch = d->character;

    ed->field_name = field_name;
    ed->text_edit_string = string;
    if (*ed->text_edit_string != NULL)
    {
	free(*ed->text_edit_string);
	*ed->text_edit_string = strdup("");
    }

    ed->single_line = is_single_line;
    ed->state = OLC_STATE_TEXTEDIT;

    send_to_char(ch, "Enter new value for '%s'%s",
		 field_name,
		 is_single_line ? ":\r\n" : "\r\n(enter . at start of line to end editting):\r\n");
}

void olc_handle_textedit(struct descriptor_data *d, struct olc_editor_s *ed, char *arg)
{
    if (ed->single_line)
    {
	if (*ed->text_edit_string != NULL)
	    free(*ed->text_edit_string);
	*ed->text_edit_string = strdup(arg);

    	ed->field_name = NULL;
	ed->text_edit_string = NULL;
	ed->single_line = 0;
	ed->state = ed->state_after;
	olc_nanny(d, "");
    }
    else if (*arg == '.')
    {
	ed->field_name = NULL;
	ed->text_edit_string = NULL;
	ed->single_line = 0;
	ed->state = ed->state_after;
	olc_nanny(d, "");
    }
    else
    {
	char *s1 = *ed->text_edit_string;
	CREATE(*ed->text_edit_string, char, strlen(s1) + strlen(arg) + 3);
	strcpy(*ed->text_edit_string, s1);
	strcat(*ed->text_edit_string, "\r\n");
	strcat(*ed->text_edit_string, arg);
    }
}

static char directions[6] = "NESWUD";

void olc_redit_display_top(struct descriptor_data *d, struct olc_editor_s *ed)
{
    char buf[MAX_STRING_LENGTH];

    struct char_data *ch = d->character;
    int room_rnum = real_room(ed->room_vnum);
    struct room_data *room = &world[room_rnum];

    send_to_char(ch, "%sRoom %d%s\r\n", CCCYN(ch, C_NRM), ed->room_vnum, CCNRM(ch, C_NRM));
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

    send_to_char(ch, "\r\nEnter Choice (or . when done): ");
    ed->state =  OLC_STATE_REDIT_TOPCHOICE;
}

void olc_redit_handle_top(struct descriptor_data *d, struct olc_editor_s *ed, char *arg)
{
    struct char_data *ch = d->character;
    int room_rnum = real_room(ed->room_vnum);
    struct room_data *room = &world[room_rnum];

    if (!*arg)
    {
	send_to_char(ch, "Not a valid choice, try again: ");
	return;
    }

    switch (*arg)
    {
      case '1':
	ed->state_after = OLC_STATE_REDIT_TOP;
	olc_start_textedit(d, ed, "name", &room->name, 1);
	break;
      case '2':
	ed->state_after = OLC_STATE_REDIT_TOP;
	olc_start_textedit(d, ed, "description", &room->description, 0);
	break;
      case '3':
	ed->state_after = OLC_STATE_REDIT_TOP;
	olc_start_toggleedit(d, ed, "room flags", &room->room_flags, room_bits);
	break;
      case '4':
	ed->state_after = OLC_STATE_REDIT_TOP;
	olc_start_typeedit(d, ed, "sector type", &room->sector_type, sector_types);
	break;
      case '.':
	olc_clear_editor(d->olc_editor_idx);
	STATE(d) = CON_PLAYING;
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
	return;
    }

    switch (ed->state)
    {
      case OLC_STATE_REDIT_TOP:
	olc_redit_display_top(d, ed);
	break;

      case OLC_STATE_REDIT_TOPCHOICE:
	olc_redit_handle_top(d, ed, arg);
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
    }
}

ACMD(do_redit)
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

    olc_create_editor(ch);

    if (ch->desc->olc_editor_idx > 0)
    {
	olc_editors[ch->desc->olc_editor_idx].state = OLC_STATE_REDIT_TOP;
	olc_editors[ch->desc->olc_editor_idx].room_vnum = room_vnum;
	olc_nanny(ch->desc, "");
	STATE(ch->desc) = CON_OLC_EDIT;
    }
}
