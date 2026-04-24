/* ************************************************************************
*   File: webserver.h                                   Part of CircleMUD *
*  Usage: Embedded HTTP server (libcivetweb) for serving mud status pages *
************************************************************************ */

#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__

void webserver_init(const char *data_dir);
void webserver_shutdown(void);
void webserver_refresh_who(void);
void webserver_olc_heartbeat(void);

#endif /* __WEBSERVER_H__ */
