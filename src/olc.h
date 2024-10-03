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

struct olc_editor_s
{
    int idnum;				/* This persons player number */
    int state;				/* olc_nanny() state */
    int room_vnum;			/* Room that we are editting. 0 is none. */

    int state_after;			/* olc_nanny() state after done editting */

    char *field_name;			/* DO NOT FREE - name of field being editted */
    char **text_edit_string;		/* String the editor is editting */
    int single_line;			/* if not 0, then editting single line. */

    int *flags_field;			/* Flags that we are editting */
    const char **bit_names;		/* Flag names */
    int n_bits;				/* Number of used bits */
};

ACMD(do_redit);
void olc_nanny(struct descriptor_data *d, char *arg);

#endif /* __olc_h__ */
