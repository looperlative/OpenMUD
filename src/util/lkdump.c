/* ************************************************************************
*  file: lkdump.c                                       Part of CircleMUD *
*  Usage: dump and validate locker control and object files               *
*                                                                         *
*  Run from the circle root directory.                                    *
*  Usage: lkdump [lockername ...]                                         *
*    No args: dump everything from lib-run/etc/lcontrol + object files.  *
*    With args: dump only the named lockers.                              *
************************************************************************ */

#include "conf.h"
#include "sysdep.h"

#include "structs.h"
#include "locker.h"

#define LCONTROL_PATH  "lib-run/etc/lcontrol"
#define LOCKERS_DIR    "lib-run/plrlockers/"

static void dump_locker_file(const char *name);
static void dump_all(void);
static void check_control(struct locker_control_rec *rec, int idx);


int main(int argc, char **argv)
{
  int i;

  printf("struct sizes: locker_control_rec=%d  obj_file_elem=%d\n\n",
      (int)sizeof(struct locker_control_rec),
      (int)sizeof(struct obj_file_elem));

  if (argc < 2) {
    dump_all();
  } else {
    for (i = 1; i < argc; i++)
      dump_locker_file(argv[i]);
  }
  return (0);
}


/* Dump every record from the lcontrol file, then each locker's object file */
static void dump_all(void)
{
  FILE *fl;
  struct locker_control_rec rec;
  int n, idx = 0;

  printf("=== %s ===\n", LCONTROL_PATH);

  fl = fopen(LCONTROL_PATH, "rb");
  if (!fl) {
    printf("  ERROR: cannot open '%s': %s\n", LCONTROL_PATH, strerror(errno));
    return;
  }

  while (!feof(fl)) {
    n = fread(&rec, sizeof(struct locker_control_rec), 1, fl);
    if (n < 1) {
      if (!feof(fl))
        printf("  ERROR: read error at record %d: %s\n", idx, strerror(errno));
      break;
    }

    printf("\n--- locker[%d] ---\n", idx);
    check_control(&rec, idx);
    dump_locker_file(rec.name);
    idx++;
  }
  fclose(fl);

  if (idx == 0)
    printf("  (no records)\n");
  else
    printf("\n%d locker record%s total.\n", idx, idx != 1 ? "s" : "");
}


static void check_control(struct locker_control_rec *rec, int idx)
{
  int j;
  int name_ok = 1;
  const char *p;

  /* Validate name */
  if (!rec->name[0]) {
    printf("  WARN: name is empty\n");
    name_ok = 0;
  } else {
    for (p = rec->name; *p; p++) {
      if (!isalnum((unsigned char)*p)) {
        printf("  WARN: name contains non-alphanumeric char 0x%02x\n",
            (unsigned char)*p);
        name_ok = 0;
        break;
      }
    }
  }

  /* Check null termination within bounds */
  if (rec->name[LOCKER_MAX_NAME_LENGTH] != '\0') {
    printf("  WARN: name is not null-terminated within bounds\n");
    name_ok = 0;
  }

  if (name_ok)
    printf("  name        : '%s'\n", rec->name);
  else
    printf("  name (raw)  : ");
  {
    int k;
    for (k = 0; k <= LOCKER_MAX_NAME_LENGTH; k++)
      printf("%02x ", (unsigned char)rec->name[k]);
    printf("\n");
  }

  printf("  owner id    : %ld\n", rec->owner);
  printf("  num_of_guests: %d\n", rec->num_of_guests);

  if (rec->num_of_guests < 0 || rec->num_of_guests > MAX_LOCKER_GUESTS) {
    printf("  WARN: num_of_guests %d is out of range [0, %d]\n",
        rec->num_of_guests, MAX_LOCKER_GUESTS);
  }

  for (j = 0; j < MAX_LOCKER_GUESTS; j++) {
    if (j < rec->num_of_guests)
      printf("  guest[%d]    : %ld%s\n", j, rec->guests[j],
          rec->guests[j] <= 0 ? "  <-- WARN: invalid id" : "");
    else if (rec->guests[j] != 0)
      printf("  guest[%d]    : %ld  <-- WARN: beyond num_of_guests but non-zero\n",
          j, rec->guests[j]);
  }

  printf("  created_on  : %ld", (long)rec->created_on);
  if (rec->created_on > 0) {
    char tbuf[64];
    strftime(tbuf, sizeof(tbuf), " (%Y-%m-%d)", localtime(&rec->created_on));
    printf("%s", tbuf);
  }
  printf("\n");

  for (j = 0; j < 8; j++) {
    if (rec->spare[j] != 0)
      printf("  spare[%d]    : %ld  <-- WARN: non-zero spare\n", j, rec->spare[j]);
  }
}


static void dump_locker_file(const char *name)
{
  FILE *fl;
  char path[256];
  struct obj_file_elem obj;
  int n, count = 0;

  snprintf(path, sizeof(path), "%s%s.locker", LOCKERS_DIR, name);
  printf("  file: %s\n", path);

  fl = fopen(path, "rb");
  if (!fl) {
    if (errno == ENOENT)
      printf("    (no object file -- locker is empty or file missing)\n");
    else
      printf("    ERROR: cannot open: %s\n", strerror(errno));
    return;
  }

  while (!feof(fl)) {
    n = fread(&obj, sizeof(struct obj_file_elem), 1, fl);
    if (n < 1) {
      if (!feof(fl))
        printf("    ERROR: read error at entry %d: %s\n", count, strerror(errno));
      break;
    }

    printf("    [%d] vnum=%-6d", count, (int)obj.item_number);
#if USE_AUTOEQ
    printf(" loc=%-3d", (int)obj.location);
#endif
    printf(" val=[%d,%d,%d,%d] flags=%d wt=%d timer=%d\n",
        obj.value[0], obj.value[1], obj.value[2], obj.value[3],
        obj.extra_flags, obj.weight, obj.timer);

    if (obj.item_number <= 0 || obj.item_number > 65534)
      printf("         WARN: item_number %d looks invalid\n", (int)obj.item_number);

    count++;
  }

  fclose(fl);

  if (count == 0)
    printf("    (file exists but contains no entries)\n");
  else
    printf("    %d item%s total.\n", count, count != 1 ? "s" : "");
}
