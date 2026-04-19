/* ************************************************************************
*  file: listrent.c                                  Part of CircleMUD *
*  Usage: list player rent files                                          *
*  Written by Jeremy Elson                                                *
*  All Rights Reserved                                                    *
*  Copyright (C) 1993 The Trustees of The Johns Hopkins University        *
************************************************************************* */

#include "conf.h"
#include "sysdep.h"

#include "structs.h"

void Crash_listrent(char *fname);

int main(int argc, char **argv)
{
  int x;

  for (x = 1; x < argc; x++)
    Crash_listrent(argv[x]);

  return (0);
}


void Crash_listrent(char *fname)
{
  FILE *fl;
  char buf[MAX_STRING_LENGTH];
  struct obj_file_elem object;
  struct rent_info rent;

  if (!(fl = fopen(fname, "rb"))) {
    sprintf(buf, "%s has no rent file.\r\n", fname);
    printf("%s", buf);
    return;
  }
  sprintf(buf, "%s\r\n", fname);
  if (!feof(fl))
    if (fread(&rent, sizeof(struct rent_info), 1, fl) < 1 && !feof(fl)) { fclose(fl); return; }
  switch (rent.rentcode) {
  case RENT_RENTED:
    strcat(buf, "Rent\r\n");
    break;
  case RENT_CRASH:
    strcat(buf, "Crash\r\n");
    break;
  case RENT_CRYO:
    strcat(buf, "Cryo\r\n");
    break;
  case RENT_TIMEDOUT:
  case RENT_FORCED:
    strcat(buf, "TimedOut\r\n");
    break;
  default:
    strcat(buf, "Undef\r\n");
    break;
  }
  while (!feof(fl)) {
    if (fread(&object, sizeof(struct obj_file_elem), 1, fl) < 1 && !feof(fl)) {
      fclose(fl);
      return;
    }
    if (ferror(fl)) {
      fclose(fl);
      return;
    }
    if (!feof(fl)) {
      size_t len = strlen(buf);
      snprintf(buf + len, sizeof(buf) - len, "[%5d] %s\n", object.item_number, fname);
    }
  }
  printf("%s", buf);
  fclose(fl);
}
