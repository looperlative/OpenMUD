/* ************************************************************************
*   File: locker.c                                      Part of CircleMUD *
*  Usage: Player locker system - named persistent per-player storage      *
************************************************************************ */

#include "conf.h"
#include "sysdep.h"

#include "structs.h"
#include "comm.h"
#include "handler.h"
#include "db.h"
#include "interpreter.h"
#include "utils.h"
#include "locker.h"
#include "constants.h"

/* External functions */
struct obj_data *Obj_from_store(struct obj_file_elem object, int *location);
int Obj_to_store(struct obj_data *obj, FILE *fl, int location);

/* External config variables */
extern int max_locker_name_length;
extern int max_locker_vnum_count;
extern int max_locker_vnum_types;
extern int max_lockers_owned;
extern int max_lockers_shared;

/* Module globals */
struct locker_control_rec locker_control[MAX_LOCKERS];
int num_of_lockers = 0;

/* Local function prototypes */
static int Locker_get_filename(const char *name, char *filename, size_t maxlen);
static void Locker_store_obj(struct char_data *ch, struct obj_data *obj, int idx);
static void Locker_retrieve_obj(struct char_data *ch, char *name_arg, int idx);
static void str_tolower_inplace(char *str);


static void str_tolower_inplace(char *str)
{
  for (; *str; str++)
    *str = LOWER(*str);
}


int Locker_valid_name(const char *name)
{
  int len = 0;
  const char *p;
  int max_len;

  if (!name || !*name)
    return (0);

  max_len = MIN(max_locker_name_length, LOCKER_MAX_NAME_LENGTH);

  for (p = name; *p; p++, len++) {
    if (!isalnum((unsigned char)*p))
      return (0);
    if (len >= max_len)
      return (0);
  }
  return (len >= 1);
}


static int Locker_get_filename(const char *name, char *filename, size_t maxlen)
{
  if (!name || !*name)
    return (0);
  snprintf(filename, maxlen, LIB_PLRLOCKERS "%s.locker", name);
  return (1);
}


int find_locker_by_name(const char *name)
{
  int i;
  for (i = 0; i < num_of_lockers; i++)
    if (!str_cmp(locker_control[i].name, name))
      return (i);
  return (NOWHERE);
}


int find_locker_owned_by(long idnum)
{
  int i;
  for (i = 0; i < num_of_lockers; i++)
    if (locker_control[i].owner == idnum)
      return (i);
  return (NOWHERE);
}


int count_lockers_shared_to(long idnum)
{
  int i, j, count = 0;
  for (i = 0; i < num_of_lockers; i++)
    for (j = 0; j < locker_control[i].num_of_guests; j++)
      if (locker_control[i].guests[j] == idnum)
        count++;
  return (count);
}


int Locker_can_access(struct char_data *ch, int idx)
{
  int j;
  if (GET_LEVEL(ch) >= LVL_GRGOD)
    return (1);
  if (GET_IDNUM(ch) == locker_control[idx].owner)
    return (1);
  for (j = 0; j < locker_control[idx].num_of_guests; j++)
    if (GET_IDNUM(ch) == locker_control[idx].guests[j])
      return (1);
  return (0);
}


void Locker_boot(void)
{
  FILE *fl;
  struct locker_control_rec temp;
  int n;

  memset(locker_control, 0, sizeof(locker_control));

  if (!(fl = fopen(LCONTROL_FILE, "rb"))) {
    if (errno == ENOENT)
      log("   Locker control file '%s' does not exist.", LCONTROL_FILE);
    else
      perror("SYSERR: " LCONTROL_FILE);
    return;
  }

  while (!feof(fl) && num_of_lockers < MAX_LOCKERS) {
    n = fread(&temp, sizeof(struct locker_control_rec), 1, fl);
    if (n < 1 || feof(fl))
      break;
    if (get_name_by_id(temp.owner) == NULL)
      continue;		/* owner deleted -- skip */
    locker_control[num_of_lockers++] = temp;
  }
  fclose(fl);
  log("   %d locker%s loaded.", num_of_lockers, num_of_lockers != 1 ? "s" : "");
}


void Locker_save_control(void)
{
  FILE *fl;

  if (!(fl = fopen(LCONTROL_FILE, "wb"))) {
    perror("SYSERR: Unable to open locker control file.");
    return;
  }
  fwrite(locker_control, sizeof(struct locker_control_rec), num_of_lockers, fl);
  fclose(fl);
}


void Locker_list_contents(struct char_data *ch, int idx)
{
  FILE *fl;
  char filename[MAX_STRING_LENGTH];
  struct obj_file_elem object;
  struct obj_data *obj;
  int i, found = 0, n;

  if (!Locker_get_filename(locker_control[idx].name, filename, sizeof(filename)))
    return;
  if (!(fl = fopen(filename, "rb"))) {
    send_to_char(ch, "The locker is empty.\r\n");
    return;
  }
  while (!feof(fl)) {
    n = fread(&object, sizeof(struct obj_file_elem), 1, fl);
    if (n < 1 || ferror(fl))
      break;
    if (feof(fl))
      break;
    if ((obj = Obj_from_store(object, &i)) != NULL) {
      send_to_char(ch, " [%5d] %s\r\n", GET_OBJ_VNUM(obj), obj->short_description);
      free_obj(obj);
      found++;
    }
  }
  fclose(fl);
  if (!found)
    send_to_char(ch, "The locker is empty.\r\n");
}


/* Count how many times target_vnum appears and how many distinct vnums are in
   the locker file.  Returns 1 on success, 0 on file error. */
static int scan_locker_file(int idx, obj_vnum target_vnum,
                             int *count_this_vnum, int *count_distinct)
{
  FILE *fl;
  char filename[MAX_STRING_LENGTH];
  struct obj_file_elem object;
  obj_vnum seen[256];
  int num_seen = 0, i, already, n;

  *count_this_vnum = 0;
  *count_distinct = 0;

  if (!Locker_get_filename(locker_control[idx].name, filename, sizeof(filename)))
    return (0);

  fl = fopen(filename, "rb");
  if (!fl)
    return (1);		/* empty locker -- not an error */

  while (!feof(fl)) {
    n = fread(&object, sizeof(struct obj_file_elem), 1, fl);
    if (n < 1 || ferror(fl) || feof(fl))
      break;

    if (object.item_number == target_vnum)
      (*count_this_vnum)++;

    already = 0;
    for (i = 0; i < num_seen; i++)
      if (seen[i] == object.item_number) { already = 1; break; }
    if (!already && num_seen < 256)
      seen[num_seen++] = object.item_number;
  }
  fclose(fl);
  *count_distinct = num_seen;
  return (1);
}


static void Locker_store_obj(struct char_data *ch, struct obj_data *obj, int idx)
{
  FILE *fl;
  char filename[MAX_STRING_LENGTH];
  int count_this_vnum, count_distinct;
  obj_vnum vnum;

  vnum = GET_OBJ_VNUM(obj);

  if (vnum == NOTHING) {
    send_to_char(ch, "That item cannot be stored in a locker.\r\n");
    return;
  }

  if (obj->contains) {
    send_to_char(ch, "Empty the container before storing it.\r\n");
    return;
  }

  if (!scan_locker_file(idx, vnum, &count_this_vnum, &count_distinct)) {
    send_to_char(ch, "Error accessing locker.\r\n");
    return;
  }

  if (count_this_vnum >= max_locker_vnum_count) {
    send_to_char(ch, "Your locker already contains the maximum number of that item (%d).\r\n",
        max_locker_vnum_count);
    return;
  }

  if (count_this_vnum == 0 && count_distinct >= max_locker_vnum_types) {
    send_to_char(ch, "Your locker already contains the maximum number of different item types (%d).\r\n",
        max_locker_vnum_types);
    return;
  }

  if (!Locker_get_filename(locker_control[idx].name, filename, sizeof(filename))) {
    send_to_char(ch, "Error accessing locker.\r\n");
    return;
  }

  if (!(fl = fopen(filename, "ab"))) {
    perror("SYSERR: Locker_store_obj fopen");
    send_to_char(ch, "Error opening locker for storage.\r\n");
    return;
  }

  if (!Obj_to_store(obj, fl, 0)) {
    fclose(fl);
    send_to_char(ch, "Error storing item.\r\n");
    return;
  }
  fclose(fl);

  act("You store $p in the locker.", FALSE, ch, obj, NULL, TO_CHAR);
  act("$n stores $p in $s locker.", FALSE, ch, obj, NULL, TO_ROOM);
  obj_from_char(obj);
  extract_obj(obj);
}


static void Locker_retrieve_obj(struct char_data *ch, char *name_arg, int idx)
{
  FILE *fl;
  char filename[MAX_STRING_LENGTH];
  struct obj_file_elem buf[MAX_LOCKER_ITEMS];
  int n_entries = 0, found_idx = -1, i, n, loc;
  struct obj_data *obj = NULL;

  if (!Locker_get_filename(locker_control[idx].name, filename, sizeof(filename))) {
    send_to_char(ch, "Error accessing locker.\r\n");
    return;
  }

  fl = fopen(filename, "rb");
  if (!fl) {
    send_to_char(ch, "The locker is empty.\r\n");
    return;
  }

  while (!feof(fl) && n_entries < MAX_LOCKER_ITEMS) {
    n = fread(&buf[n_entries], sizeof(struct obj_file_elem), 1, fl);
    if (n < 1 || ferror(fl) || feof(fl))
      break;
    n_entries++;
  }
  fclose(fl);

  if (n_entries == 0) {
    send_to_char(ch, "The locker is empty.\r\n");
    return;
  }

  for (i = 0; i < n_entries && found_idx < 0; i++) {
    struct obj_data *tmp = Obj_from_store(buf[i], &loc);
    if (!tmp) continue;
    if (isname(name_arg, tmp->name)) {
      found_idx = i;
      obj = tmp;
    } else {
      free_obj(tmp);
    }
  }

  if (found_idx < 0) {
    send_to_char(ch, "That item is not in the locker.\r\n");
    return;
  }

  if (!CAN_CARRY_OBJ(ch, obj)) {
    free_obj(obj);
    send_to_char(ch, "You can't carry that much weight.\r\n");
    return;
  }

  /* Rewrite file without the retrieved entry */
  if (!(fl = fopen(filename, "wb"))) {
    perror("SYSERR: Locker_retrieve_obj fopen wb");
    free_obj(obj);
    send_to_char(ch, "Error rewriting locker file.\r\n");
    return;
  }
  for (i = 0; i < n_entries; i++) {
    if (i == found_idx) continue;
    fwrite(&buf[i], sizeof(struct obj_file_elem), 1, fl);
  }
  fclose(fl);

  obj_to_char(obj, ch);
  act("You retrieve $p from the locker.", FALSE, ch, obj, NULL, TO_CHAR);
  act("$n retrieves $p from $s locker.", FALSE, ch, obj, NULL, TO_ROOM);
}


ACMD(do_locker)
{
  char arg1[MAX_INPUT_LENGTH], arg2[MAX_INPUT_LENGTH], arg3[MAX_INPUT_LENGTH];
  char lname[LOCKER_MAX_NAME_LENGTH + 1];
  int idx, i, j, owned;
  long target_id;
  struct obj_data *obj;
  char *owner_name, *gname;

  if (!ROOM_FLAGGED(IN_ROOM(ch), ROOM_LOCKER)) {
    send_to_char(ch, "You must be in the locker room to do that.\r\n");
    return;
  }

  argument = one_argument(argument, arg1);

  if (!*arg1 || is_abbrev(arg1, "list")) {
    int shared = 0, found = 0;
    send_to_char(ch, "Your lockers:\r\n");
    for (i = 0; i < num_of_lockers; i++) {
      if (locker_control[i].owner == GET_IDNUM(ch)) {
        send_to_char(ch, "  %-20s (%d guest%s)\r\n",
            locker_control[i].name,
            locker_control[i].num_of_guests,
            locker_control[i].num_of_guests != 1 ? "s" : "");
        found++;
      }
    }
    if (!found)
      send_to_char(ch, "  None.\r\n");
    for (i = 0; i < num_of_lockers; i++) {
      for (j = 0; j < locker_control[i].num_of_guests; j++) {
        if (locker_control[i].guests[j] == GET_IDNUM(ch)) {
          if (!shared) send_to_char(ch, "Lockers shared with you:\r\n");
          owner_name = get_name_by_id(locker_control[i].owner);
          send_to_char(ch, "  %-20s (owned by %s)\r\n",
              locker_control[i].name,
              owner_name ? owner_name : "<unknown>");
          shared++;
        }
      }
    }
    return;
  }

  if (is_abbrev(arg1, "create")) {
    one_argument(argument, arg2);
    if (!*arg2) {
      send_to_char(ch, "Create a locker with what name?\r\n");
      return;
    }
    strlcpy(lname, arg2, sizeof(lname));
    str_tolower_inplace(lname);
    if (!Locker_valid_name(lname)) {
      send_to_char(ch, "Locker names must be 1-%d alphanumeric characters only.\r\n",
          MIN(max_locker_name_length, LOCKER_MAX_NAME_LENGTH));
      return;
    }
    if (find_locker_by_name(lname) != NOWHERE) {
      send_to_char(ch, "A locker named '%s' already exists.\r\n", lname);
      return;
    }
    owned = 0;
    for (i = 0; i < num_of_lockers; i++)
      if (locker_control[i].owner == GET_IDNUM(ch))
        owned++;
    if (owned >= max_lockers_owned) {
      send_to_char(ch, "You already own the maximum number of lockers (%d).\r\n",
          max_lockers_owned);
      return;
    }
    if (num_of_lockers >= MAX_LOCKERS) {
      send_to_char(ch, "No more lockers are available at this time.\r\n");
      return;
    }
    memset(&locker_control[num_of_lockers], 0, sizeof(struct locker_control_rec));
    strlcpy(locker_control[num_of_lockers].name, lname, LOCKER_MAX_NAME_LENGTH + 1);
    locker_control[num_of_lockers].owner = GET_IDNUM(ch);
    locker_control[num_of_lockers].created_on = time(0);
    num_of_lockers++;
    Locker_save_control();
    send_to_char(ch, "Locker '%s' created.\r\n", lname);
    mudlog(NRM, LVL_GOD, TRUE, "%s created locker '%s'.", GET_NAME(ch), lname);
    return;
  }

  if (is_abbrev(arg1, "delete")) {
    char filename[MAX_STRING_LENGTH];
    one_argument(argument, arg2);
    if (!*arg2) {
      send_to_char(ch, "Delete which locker?\r\n");
      return;
    }
    strlcpy(lname, arg2, sizeof(lname));
    str_tolower_inplace(lname);
    idx = find_locker_by_name(lname);
    if (idx == NOWHERE) {
      send_to_char(ch, "No locker named '%s' exists.\r\n", lname);
      return;
    }
    if (locker_control[idx].owner != GET_IDNUM(ch)) {
      send_to_char(ch, "You don't own that locker.\r\n");
      return;
    }
    Locker_get_filename(lname, filename, sizeof(filename));
    remove(filename);
    for (j = idx; j < num_of_lockers - 1; j++)
      locker_control[j] = locker_control[j + 1];
    num_of_lockers--;
    Locker_save_control();
    send_to_char(ch, "Locker '%s' deleted.\r\n", lname);
    mudlog(NRM, LVL_GOD, TRUE, "%s deleted locker '%s'.", GET_NAME(ch), lname);
    return;
  }

  if (is_abbrev(arg1, "show")) {
    one_argument(argument, arg2);
    if (!*arg2) {
      send_to_char(ch, "Show which locker?\r\n");
      return;
    }
    strlcpy(lname, arg2, sizeof(lname));
    str_tolower_inplace(lname);
    idx = find_locker_by_name(lname);
    if (idx == NOWHERE) {
      send_to_char(ch, "No locker named '%s' exists.\r\n", lname);
      return;
    }
    if (!Locker_can_access(ch, idx)) {
      send_to_char(ch, "You don't have access to that locker.\r\n");
      return;
    }
    send_to_char(ch, "Contents of locker '%s':\r\n", lname);
    Locker_list_contents(ch, idx);
    return;
  }

  if (is_abbrev(arg1, "put")) {
    argument = one_argument(argument, arg2);
    one_argument(argument, arg3);
    if (!*arg2 || !*arg3) {
      send_to_char(ch, "Usage: locker put <item> <lockername>\r\n");
      return;
    }
    strlcpy(lname, arg3, sizeof(lname));
    str_tolower_inplace(lname);
    idx = find_locker_by_name(lname);
    if (idx == NOWHERE) {
      send_to_char(ch, "No locker named '%s' exists.\r\n", lname);
      return;
    }
    if (!Locker_can_access(ch, idx)) {
      send_to_char(ch, "You don't have access to that locker.\r\n");
      return;
    }
    obj = get_obj_in_list_vis(ch, arg2, NULL, ch->carrying);
    if (!obj) {
      send_to_char(ch, "You don't have that item.\r\n");
      return;
    }
    Locker_store_obj(ch, obj, idx);
    return;
  }

  if (is_abbrev(arg1, "get")) {
    argument = one_argument(argument, arg2);
    one_argument(argument, arg3);
    if (!*arg2 || !*arg3) {
      send_to_char(ch, "Usage: locker get <item> <lockername>\r\n");
      return;
    }
    strlcpy(lname, arg3, sizeof(lname));
    str_tolower_inplace(lname);
    idx = find_locker_by_name(lname);
    if (idx == NOWHERE) {
      send_to_char(ch, "No locker named '%s' exists.\r\n", lname);
      return;
    }
    if (!Locker_can_access(ch, idx)) {
      send_to_char(ch, "You don't have access to that locker.\r\n");
      return;
    }
    Locker_retrieve_obj(ch, arg2, idx);
    return;
  }

  if (is_abbrev(arg1, "share")) {
    argument = one_argument(argument, arg2);
    one_argument(argument, arg3);
    if (!*arg2 || !*arg3) {
      send_to_char(ch, "Usage: locker share <lockername> <player>\r\n");
      return;
    }
    strlcpy(lname, arg2, sizeof(lname));
    str_tolower_inplace(lname);
    idx = find_locker_by_name(lname);
    if (idx == NOWHERE) {
      send_to_char(ch, "No locker named '%s' exists.\r\n", lname);
      return;
    }
    if (locker_control[idx].owner != GET_IDNUM(ch)) {
      send_to_char(ch, "You don't own that locker.\r\n");
      return;
    }
    target_id = get_id_by_name(arg3);
    if (target_id < 0) {
      send_to_char(ch, "Unknown player '%s'.\r\n", arg3);
      return;
    }
    if (target_id == GET_IDNUM(ch)) {
      send_to_char(ch, "It's your locker!\r\n");
      return;
    }

    /* Toggle: if already a guest, remove them */
    for (j = 0; j < locker_control[idx].num_of_guests; j++) {
      if (locker_control[idx].guests[j] == target_id) {
        for (; j < locker_control[idx].num_of_guests - 1; j++)
          locker_control[idx].guests[j] = locker_control[idx].guests[j + 1];
        locker_control[idx].num_of_guests--;
        Locker_save_control();
        send_to_char(ch, "Access to locker '%s' revoked from %s.\r\n", lname, arg3);
        return;
      }
    }

    /* Add guest */
    if (locker_control[idx].num_of_guests >= MAX_LOCKER_GUESTS) {
      send_to_char(ch, "That locker already has the maximum number of guests.\r\n");
      return;
    }
    if (count_lockers_shared_to(target_id) >= max_lockers_shared) {
      send_to_char(ch, "%s already has the maximum number of lockers shared with them (%d).\r\n",
          arg3, max_lockers_shared);
      return;
    }
    send_to_char(ch,
        "Warning: %s will be able to remove any item from locker '%s'.\r\n",
        arg3, lname);
    locker_control[idx].guests[locker_control[idx].num_of_guests++] = target_id;
    Locker_save_control();
    send_to_char(ch, "Access to locker '%s' granted to %s.\r\n", lname, arg3);
    gname = get_name_by_id(target_id);
    mudlog(NRM, LVL_GOD, TRUE, "%s shared locker '%s' with %s.",
        GET_NAME(ch), lname, gname ? gname : arg3);
    return;
  }

  send_to_char(ch,
      "Usage: locker { list | create <name> | delete <name> | show <name> |\r\n"
      "               put <item> <name> | get <item> <name> | share <name> <player> }\r\n");
}


ACMD(do_lcontrol)
{
  char arg1[MAX_INPUT_LENGTH], arg2[MAX_INPUT_LENGTH];
  char lname[LOCKER_MAX_NAME_LENGTH + 1];
  char filename[MAX_STRING_LENGTH];
  char datebuf[16];
  int idx, i, j;
  char *owner_name, *gname, *t;

  half_chop(argument, arg1, arg2);

  if (is_abbrev(arg1, "list")) {
    if (!num_of_lockers) {
      send_to_char(ch, "No lockers defined.\r\n");
      return;
    }
    send_to_char(ch,
        "%-20s  %-15s  Guests  Created\r\n"
        "--------------------  ---------------  ------  ----------\r\n",
        "Name", "Owner");
    for (i = 0; i < num_of_lockers; i++) {
      owner_name = get_name_by_id(locker_control[i].owner);
      if (locker_control[i].created_on) {
        t = asctime(localtime(&locker_control[i].created_on));
        *(t + 10) = '\0';
        strlcpy(datebuf, t, sizeof(datebuf));
      } else {
        strcpy(datebuf, "Unknown");		/* strcpy: OK (strlen("Unknown") < 16) */
      }
      send_to_char(ch, "%-20s  %-15s  %6d  %s\r\n",
          locker_control[i].name,
          owner_name ? owner_name : "<deleted>",
          locker_control[i].num_of_guests,
          datebuf);
    }
    return;
  }

  if (is_abbrev(arg1, "show")) {
    if (!*arg2) {
      send_to_char(ch, "Show which locker?\r\n");
      return;
    }
    strlcpy(lname, arg2, sizeof(lname));
    str_tolower_inplace(lname);
    idx = find_locker_by_name(lname);
    if (idx == NOWHERE) {
      send_to_char(ch, "No locker named '%s'.\r\n", lname);
      return;
    }
    owner_name = get_name_by_id(locker_control[idx].owner);
    send_to_char(ch, "Locker: %-20s  Owner: %s  Guests: %d\r\n",
        locker_control[idx].name,
        owner_name ? owner_name : "<deleted>",
        locker_control[idx].num_of_guests);
    for (j = 0; j < locker_control[idx].num_of_guests; j++) {
      gname = get_name_by_id(locker_control[idx].guests[j]);
      send_to_char(ch, "  Guest: %s\r\n", gname ? gname : "<deleted>");
    }
    send_to_char(ch, "Contents:\r\n");
    Locker_list_contents(ch, idx);
    return;
  }

  if (is_abbrev(arg1, "delete")) {
    if (!*arg2) {
      send_to_char(ch, "Delete which locker?\r\n");
      return;
    }
    strlcpy(lname, arg2, sizeof(lname));
    str_tolower_inplace(lname);
    idx = find_locker_by_name(lname);
    if (idx == NOWHERE) {
      send_to_char(ch, "No locker named '%s'.\r\n", lname);
      return;
    }
    Locker_get_filename(lname, filename, sizeof(filename));
    remove(filename);
    for (j = idx; j < num_of_lockers - 1; j++)
      locker_control[j] = locker_control[j + 1];
    num_of_lockers--;
    Locker_save_control();
    send_to_char(ch, "Locker '%s' deleted.\r\n", lname);
    mudlog(NRM, MAX(LVL_GRGOD, GET_INVIS_LEV(ch)), TRUE,
        "%s force-deleted locker '%s'.", GET_NAME(ch), lname);
    return;
  }

  send_to_char(ch, "Usage: lcontrol { list | show <name> | delete <name> }\r\n");
}
