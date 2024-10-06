/* OLC.H
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

#ifndef __olc_h__
#define __olc_h__

#define MAX_EDITORS			100

#define OLC_STATE_REDIT_TOP		1
#define OLC_STATE_REDIT_TOPCHOICE	2
#define OLC_STATE_TEXTEDIT		3
#define OLC_STATE_TOGGLEEDIT		4
#define OLC_STATE_TYPEEDIT		5
#define OLC_STATE_DIRECTION_TOP		6
#define OLC_STATE_DIRECTION_TOPCHOICE	7
#define OLC_STATE_DOOR_TO_ROOM		8
#define OLC_STATE_DOOR_KEY_NUMBER	9
#define OLC_STATE_EXTRADESC_TOP		10
#define OLC_STATE_EXTRADESC_TOPCHOICE	11
#define OLC_STATE_MEDIT_TOP		12
#define OLC_STATE_MEDIT_TOPCHOICE	13
#define OLC_STATE_NUMBER		14

struct olc_garbage_s
{
    struct olc_garbage_s *next;
    int vnum;
    void *garbage;
};

struct olc_editor_s
{
    int idnum;				/* This persons player number */
    int state;				/* olc_nanny() state */
    struct olc_garbage_s **garbage_list;/* Where to put our garbage. */

    /* Specific fields for specific top levels */
    int vnum;				/* vnum of what we are editing. 0 is none. */
    int direction;			/* Direction that we are editing. 0 is none. */

    /* Fields for generic editors. */
    int state_after[2];			/* olc_nanny() state after done editing */

    /* Fields for text editor */
    char *field_name;			/* DO NOT FREE - name of field being edited */
    char **text_edit_string;		/* String the editor is editing */
    int single_line;			/* if not 0, then editing single line. */
    int want_dice;			/* Want something of the form: 1d2+3 */

    /* Fields for bit and type editors */
    int *flags_field;			/* Flags that we are editing */
    const char **bit_names;		/* Flag names */
    int n_bits;				/* Number of used bits */

    /* Extra description editor */
    struct extra_descr_data **extra_desc_list; /* List that the extra desc comes from. */
    struct extra_descr_data *extra_desc; /* Extra desc being edited. */

    /* Number entry */
    void *number_field;
    int number_field_size;
    int number_min;
    int number_max;

    /* Dice */
    int *ndice;
    int *sizedice;
    int *diceextra32;
    sbyte *diceextra8;
};

ACMD(do_medit);
ACMD(do_redit);
void olc_nanny(struct descriptor_data *d, char *arg);

#endif /* __olc_h__ */
