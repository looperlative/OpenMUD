/* ***********************************************************************
*  File: eqset.c                                  A utility to CircleMUD *
* Usage: writing/reading player's equipment sets.                        *
*                                                                        *
* Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
* CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.              *
*********************************************************************** */

#include "conf.h"
#include "sysdep.h"
#include <sys/stat.h>
#include "structs.h"
#include "utils.h"
#include "db.h"

void write_eqsets(struct char_data *ch);
void read_eqsets(struct char_data *ch);
void free_eqsets(struct char_data *ch);
void delete_eqsets(const char *charname);

static void mkdir_for_file(const char *filepath)
{
  char dir[PATH_MAX];
  char *slash;

  strlcpy(dir, filepath, sizeof(dir));
  if ((slash = strrchr(dir, '/')) == NULL)
    return;
  *slash = '\0';

  if (mkdir(dir, 0755) == 0 || errno == EEXIST)
    return;

  /* Parent may also be missing; try creating it first */
  if ((slash = strrchr(dir, '/')) != NULL) {
    *slash = '\0';
    mkdir(dir, 0755);
    *slash = '/';
  }
  mkdir(dir, 0755);
}

void write_eqsets(struct char_data *ch)
{
  FILE *file;
  char fn[MAX_STRING_LENGTH];
  struct eqset_data *s;
  int i;

  get_filename(fn, sizeof(fn), EQSET_FILE, GET_NAME(ch));
  mkdir_for_file(fn);
  remove(fn);

  if (GET_EQSETS(ch) == NULL)
    return;

  if ((file = fopen(fn, "w")) == NULL) {
    log("SYSERR: Couldn't save eqsets for %s in '%s'.", GET_NAME(ch), fn);
    perror("SYSERR: write_eqsets");
    return;
  }

  for (s = GET_EQSETS(ch); s; s = s->next) {
    fprintf(file, "%s\n", s->name);
    for (i = 0; i < NUM_WEARS; i++)
      fprintf(file, "%d\n", (int)s->vnums[i]);
  }

  fclose(file);
}

void read_eqsets(struct char_data *ch)
{
  FILE *file;
  char buf[MAX_STRING_LENGTH];
  struct eqset_data *s, *last = NULL;
  int i;

  get_filename(buf, sizeof(buf), EQSET_FILE, GET_NAME(ch));

  if ((file = fopen(buf, "r")) == NULL) {
    if (errno != ENOENT) {
      log("SYSERR: Couldn't open eqset file '%s' for %s.", buf, GET_NAME(ch));
      perror("SYSERR: read_eqsets");
    }
    return;
  }

  while (get_line(file, buf)) {
    CREATE(s, struct eqset_data, 1);
    strlcpy(s->name, buf, sizeof(s->name));

    for (i = 0; i < NUM_WEARS; i++) {
      if (!get_line(file, buf)) {
        free(s);
        fclose(file);
        return;
      }
      s->vnums[i] = (obj_vnum)atoi(buf);
    }

    s->next = NULL;
    if (last)
      last->next = s;
    else
      GET_EQSETS(ch) = s;
    last = s;
  }

  fclose(file);
}

void free_eqsets(struct char_data *ch)
{
  struct eqset_data *s, *next_s;

  for (s = GET_EQSETS(ch); s; s = next_s) {
    next_s = s->next;
    free(s);
  }
  GET_EQSETS(ch) = NULL;
}

void delete_eqsets(const char *charname)
{
  char filename[PATH_MAX];

  if (!get_filename(filename, sizeof(filename), EQSET_FILE, charname))
    return;

  if (remove(filename) < 0 && errno != ENOENT)
    log("SYSERR: deleting eqset file %s: %s", filename, strerror(errno));
}
