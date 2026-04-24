/* ************************************************************************
*   File: webserver_olc.h                                Part of CircleMUD *
*  Usage: Web-based OLC via embedded HTTP server (libcivetweb)             *
************************************************************************ */

#ifndef __WEBSERVER_OLC_H__
#define __WEBSERVER_OLC_H__

#ifdef HAVE_CIVETWEB
#include <civetweb.h>
void wolc_register_handlers(struct mg_context *ctx);
void wolc_shutdown(void);
#endif

void webserver_olc_heartbeat(void);

#endif /* __WEBSERVER_OLC_H__ */
