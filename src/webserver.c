/* ************************************************************************
*   File: webserver.c                                   Part of CircleMUD *
*  Usage: Embedded HTTP server (libcivetweb) for serving mud status pages *
************************************************************************ */

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "db.h"
#include "webserver.h"
#include "webserver_olc.h"

#ifdef HAVE_CIVETWEB

#include <civetweb.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>

#define WEB_PORT   "127.0.0.1:4445"
#define MUDWHO_URI "/mud/who.html"
#define WHO_MAX    200

extern struct descriptor_data *descriptor_list;
extern char *class_abbrevs[];

static struct mg_context *web_ctx = NULL;

struct who_entry {
  int  level;
  char cls[4];
  char name[MAX_NAME_LENGTH + 1];
  char title[MAX_TITLE_LENGTH + 1];
};

struct who_snapshot {
  struct who_entry entries[WHO_MAX];
  int count;
};

static struct who_snapshot who_snap;
static pthread_mutex_t     who_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Called from the game loop (single-threaded) — reads MUD data structures
 * without any lock, then swaps the result into who_snap under a brief mutex.
 */
void webserver_refresh_who(void)
{
  struct who_snapshot snap;
  struct descriptor_data *d;
  struct char_data *ch;

  snap.count = 0;
  for (d = descriptor_list; d && snap.count < WHO_MAX; d = d->next) {
    if (d->connected)
      continue;
    ch = d->original ? d->original : d->character;
    if (!ch)
      continue;
    if (GET_LEVEL(ch) >= LVL_IMMORT && GET_INVIS_LEV(ch))
      continue;

    snap.entries[snap.count].level = GET_LEVEL(ch);
    strncpy(snap.entries[snap.count].cls,   CLASS_ABBR(ch),
            sizeof(snap.entries[0].cls)   - 1);
    strncpy(snap.entries[snap.count].name,  GET_NAME(ch),
            sizeof(snap.entries[0].name)  - 1);
    strncpy(snap.entries[snap.count].title, GET_TITLE(ch) ? GET_TITLE(ch) : "",
            sizeof(snap.entries[0].title) - 1);
    snap.entries[snap.count].cls  [sizeof(snap.entries[0].cls)   - 1] = '\0';
    snap.entries[snap.count].name [sizeof(snap.entries[0].name)  - 1] = '\0';
    snap.entries[snap.count].title[sizeof(snap.entries[0].title) - 1] = '\0';
    snap.count++;
  }

  pthread_mutex_lock(&who_mutex);
  who_snap = snap;
  pthread_mutex_unlock(&who_mutex);
}

static int mudwho_handler(struct mg_connection *conn, void *cbdata)
{
  struct who_snapshot snap;
  char *buf;
  int pos = 0, i;

  pthread_mutex_lock(&who_mutex);
  snap = who_snap;
  pthread_mutex_unlock(&who_mutex);

#define RESPONSE_BUF_SIZE 65536
  buf = malloc(RESPONSE_BUF_SIZE);
  if (!buf) {
    mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
    return 1;
  }

#define W(fmt, ...) pos += snprintf(buf + pos, RESPONSE_BUF_SIZE - pos, fmt, ##__VA_ARGS__)

  W("<!DOCTYPE html>\n<html>\n<head><title>Who is on the Mud?</title>\n<style>\n");
  W("body { font-weight: 300; }\n");
  W("table { border-collapse: collapse; }\n");
  W("td { border: 2px solid lightblue; padding: 1px 1em; font-size: 16pt; font-weight: bold; }\n");
  W("</style>\n</head>\n<body>\n<h1>Who is playing right now?</h1>\n<table>\n");

  if (snap.count == 0) {
    W("<tr><td colspan=\"4\">Nobody is logged on at the moment.</td></tr>\n");
  } else {
    for (i = 0; i < snap.count; i++)
      W("<tr><td>%d</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",
        snap.entries[i].level, snap.entries[i].cls,
        snap.entries[i].name,  snap.entries[i].title);
  }

  W("</table>\n</body></html>\n");
#undef W

  mg_printf(conn,
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "Content-Length: %d\r\n"
      "\r\n", pos);
  mg_write(conn, buf, pos);
  free(buf);
  return 1;
}

void webserver_init(const char *data_dir)
{
  char www_root[512];
  struct mg_callbacks callbacks;

  snprintf(www_root, sizeof(www_root), "%s/www", data_dir);

  const char *options[] = {
    "listening_ports", WEB_PORT,
    "document_root",   www_root,
    NULL
  };

  memset(&callbacks, 0, sizeof(callbacks));
  web_ctx = mg_start(&callbacks, NULL, options);
  if (!web_ctx) {
    log("WEBSERVER: Failed to start on %s", WEB_PORT);
    return;
  }

  mg_set_request_handler(web_ctx, MUDWHO_URI, mudwho_handler, NULL);
  wolc_register_handlers(web_ctx);
  log("WEBSERVER: Listening on http://%s/", WEB_PORT);
}

void webserver_shutdown(void)
{
  if (web_ctx) {
    mg_stop(web_ctx);
    web_ctx = NULL;
    log("WEBSERVER: Stopped.");
  }
  wolc_shutdown();
}

#else /* HAVE_CIVETWEB not defined — empty stubs */

void webserver_init(const char *data_dir) { }
void webserver_shutdown(void)             { }
void webserver_refresh_who(void)          { }

#endif /* HAVE_CIVETWEB */
