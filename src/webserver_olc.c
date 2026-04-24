/* ************************************************************************
*   File: webserver_olc.c                                Part of CircleMUD *
*  Usage: Web-based OLC via embedded HTTP server (libcivetweb)             *
************************************************************************ */

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "db.h"
#include "olc.h"
#include "webserver_olc.h"

#ifdef HAVE_CIVETWEB

#include <civetweb.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

extern struct player_index_element *player_table;
extern int top_of_p_table;
extern FILE *player_fl;
extern struct zone_data *zone_table;
extern zone_rnum top_of_zone_table;
extern struct room_data *world;
extern room_rnum top_of_world;
extern struct char_data *mob_proto;
extern struct index_data *mob_index;
extern mob_rnum top_of_mobt;
extern struct obj_data *obj_proto;
extern struct index_data *obj_index;
extern obj_rnum top_of_objt;

/* request types */
#define WOLC_REQ_AUTH         1
#define WOLC_REQ_GET_ZONES    2
#define WOLC_REQ_GET_ROOM     3
#define WOLC_REQ_SET_ROOM     4
#define WOLC_REQ_GET_MOB      5
#define WOLC_REQ_SET_MOB      6
#define WOLC_REQ_GET_OBJ      7
#define WOLC_REQ_SET_OBJ      8
#define WOLC_REQ_GET_ZCMDS    9
#define WOLC_REQ_SET_ZCMDS   10

/* status codes */
#define WOLC_OK           0
#define WOLC_ERR_NOTFOUND 1
#define WOLC_ERR_NOPERM   2
#define WOLC_ERR_BADPW    3
#define WOLC_ERR_NOUSER   4
#define WOLC_ERR_INTERNAL 5

#define WOLC_MAX_SESSIONS 64
#define WOLC_SESSION_TTL  3600
#define WOLC_TOKEN_LEN    32

struct wolc_request {
    int  type;
    int  vnum;
    int  pfilepos;
    int  level;
    char *json_body;
    char auth_name[MAX_NAME_LENGTH + 1];
    char auth_pw[MAX_INPUT_LENGTH];
    /* response */
    int  status;
    char *response_json;
    int  out_pfilepos;
    int  out_level;
    char out_name[MAX_NAME_LENGTH + 1];
    /* sync */
    pthread_mutex_t lock;
    pthread_cond_t  done_cond;
    int             done;
    struct wolc_request *next;
};

struct wolc_session {
    char   token[WOLC_TOKEN_LEN + 1];
    int    pfilepos;
    int    level;
    char   name[MAX_NAME_LENGTH + 1];
    time_t last_used;
    int    in_use;
};

static struct wolc_request *wolc_queue_head = NULL;
static struct wolc_request *wolc_queue_tail = NULL;
static pthread_mutex_t      wolc_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct wolc_session  wolc_sessions[WOLC_MAX_SESSIONS];
static pthread_mutex_t      wolc_session_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ======================================================================
 * JSON builder
 * ====================================================================== */

struct jbuf {
    char *data;
    int   pos;
    int   cap;
};

static void jb_init(struct jbuf *b, int cap)
{
    b->data = malloc(cap);
    b->pos  = 0;
    b->cap  = cap;
    if (b->data) b->data[0] = '\0';
}

static void jb_grow(struct jbuf *b, int needed)
{
    while (b->pos + needed >= b->cap) {
        b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
}

static void jb_append(struct jbuf *b, const char *s)
{
    int len = s ? (int)strlen(s) : 0;
    jb_grow(b, len + 1);
    memcpy(b->data + b->pos, s, len);
    b->pos += len;
    b->data[b->pos] = '\0';
}

static void jb_appendf(struct jbuf *b, const char *fmt, ...)
{
    va_list ap;
    int n;
    jb_grow(b, 256);
    for (;;) {
        va_start(ap, fmt);
        n = vsnprintf(b->data + b->pos, b->cap - b->pos, fmt, ap);
        va_end(ap);
        if (n >= 0 && n < b->cap - b->pos) { b->pos += n; break; }
        jb_grow(b, b->cap);
    }
}

static void jb_escape_str(struct jbuf *b, const char *src)
{
    jb_append(b, "\"");
    if (!src) { jb_append(b, "\""); return; }
    for (; *src; src++) {
        unsigned char c = (unsigned char)*src;
        if      (c == '\\') jb_append(b, "\\\\");
        else if (c == '"')  jb_append(b, "\\\"");
        else if (c == '\r') jb_append(b, "\\r");
        else if (c == '\n') jb_append(b, "\\n");
        else if (c < 0x20)  jb_appendf(b, "\\u%04x", c);
        else { char t[2] = {(char)c, 0}; jb_append(b, t); }
    }
    jb_append(b, "\"");
}

/* ======================================================================
 * JSON parser (minimal)
 * ====================================================================== */

static const char *jp_skip(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

static const char *jp_find_key(const char *json, const char *key)
{
    char q[256];
    snprintf(q, sizeof(q), "\"%s\"", key);
    const char *p = strstr(json, q);
    if (!p) return NULL;
    p += strlen(q);
    p = jp_skip(p);
    if (!p || *p != ':') return NULL;
    return jp_skip(p + 1);
}

static char *json_get_str(const char *json, const char *key)
{
    const char *p = jp_find_key(json, key);
    if (!p || *p != '"') return NULL;
    p++;
    int cap = 4096;
    char *out = malloc(cap);
    int pos = 0;
    while (*p && *p != '"') {
        if (pos + 8 >= cap) { cap *= 2; out = realloc(out, cap); }
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"':  out[pos++] = '"';  break;
                case '\\': out[pos++] = '\\'; break;
                case '/':  out[pos++] = '/';  break;
                case 'n':  out[pos++] = '\n'; break;
                case 'r':  out[pos++] = '\r'; break;
                case 't':  out[pos++] = '\t'; break;
                default:   out[pos++] = *p;   break;
            }
            if (*p) p++;
        } else {
            out[pos++] = *p++;
        }
    }
    out[pos] = '\0';
    return out;
}

static int json_get_int(const char *json, const char *key, int *out)
{
    const char *p = jp_find_key(json, key);
    if (!p) return 0;
    char *end;
    long v = strtol(p, &end, 10);
    if (end == p) return 0;
    *out = (int)v;
    return 1;
}

static const char *json_get_array(const char *json, const char *key)
{
    const char *p = jp_find_key(json, key);
    if (!p || *p != '[') return NULL;
    return p;
}

/* Skip a JSON value, returning pointer past it */
static const char *jp_skip_value(const char *p)
{
    p = jp_skip(p);
    if (!p) return p;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') { if (*p == '\\' && *(p+1)) p++; p++; }
        if (*p == '"') p++;
    } else if (*p == '{' || *p == '[') {
        char open = *p, close = (*p == '{') ? '}' : ']';
        int d = 0;
        while (*p) {
            if (*p == open)  d++;
            else if (*p == close) { d--; if (!d) { p++; break; } }
            else if (*p == '"') { p++; while (*p && *p != '"') { if (*p=='\\' && *(p+1)) p++; p++; } }
            p++;
        }
    } else {
        while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    }
    return p;
}

/* Extract a JSON object at 'p' (pointing at '{') into a malloc'd string */
static char *jp_extract_object(const char **pp)
{
    const char *p = *pp;
    if (*p != '{') return NULL;
    const char *start = p;
    int d = 0;
    while (*p) {
        if (*p == '{') d++;
        else if (*p == '}') { d--; if (!d) { p++; break; } }
        else if (*p == '"') { p++; while (*p && *p != '"') { if (*p=='\\' && *(p+1)) p++; p++; } }
        p++;
    }
    int len = (int)(p - start);
    char *obj = malloc(len + 1);
    memcpy(obj, start, len);
    obj[len] = '\0';
    *pp = p;
    return obj;
}

/* ======================================================================
 * Extra description helpers
 * ====================================================================== */

static void jb_extra_descs(struct jbuf *b, struct extra_descr_data *ed)
{
    jb_append(b, "[");
    int first = 1;
    for (; ed; ed = ed->next) {
        if (!first) jb_append(b, ",");
        first = 0;
        jb_append(b, "{\"keyword\":");  jb_escape_str(b, ed->keyword);
        jb_append(b, ",\"description\":"); jb_escape_str(b, ed->description);
        jb_append(b, "}");
    }
    jb_append(b, "]");
}

static struct extra_descr_data *parse_extra_descs(const char *json)
{
    const char *arr = json_get_array(json, "extra_descs");
    if (!arr) return NULL;
    arr++;  /* skip '[' */
    struct extra_descr_data *head = NULL, **tail = &head;
    while (*(arr = jp_skip(arr)) && *arr != ']') {
        if (*arr != '{') { arr = jp_skip_value(arr); continue; }
        char *obj = jp_extract_object(&arr);
        char *kw   = json_get_str(obj, "keyword");
        char *desc = json_get_str(obj, "description");
        if (kw && desc) {
            struct extra_descr_data *e = calloc(1, sizeof(*e));
            e->keyword     = strdup(kw);
            e->description = strdup(desc);
            *tail = e; tail = &e->next;
        }
        free(kw); free(desc); free(obj);
        arr = jp_skip(arr);
        if (*arr == ',') arr++;
    }
    return head;
}

/* ======================================================================
 * Queue mechanics
 * ====================================================================== */

static struct wolc_request *wolc_request_alloc(int type)
{
    struct wolc_request *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->type = type;
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->done_cond, NULL);
    return r;
}

static void wolc_request_free(struct wolc_request *r)
{
    if (!r) return;
    free(r->json_body);
    free(r->response_json);
    pthread_mutex_destroy(&r->lock);
    pthread_cond_destroy(&r->done_cond);
    free(r);
}

static void wolc_enqueue(struct wolc_request *r)
{
    r->next = NULL;
    pthread_mutex_lock(&wolc_queue_mutex);
    if (!wolc_queue_tail) wolc_queue_head = wolc_queue_tail = r;
    else { wolc_queue_tail->next = r; wolc_queue_tail = r; }
    pthread_mutex_unlock(&wolc_queue_mutex);
}

static int wolc_wait(struct wolc_request *r)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;
    pthread_mutex_lock(&r->lock);
    while (!r->done) {
        if (pthread_cond_timedwait(&r->done_cond, &r->lock, &ts) != 0) {
            pthread_mutex_unlock(&r->lock);
            return -1;
        }
    }
    pthread_mutex_unlock(&r->lock);
    return 0;
}

/* ======================================================================
 * Session management
 * ====================================================================== */

static void wolc_gen_token(char out[WOLC_TOKEN_LEN + 1])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char buf[WOLC_TOKEN_LEN / 2];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t nr = read(fd, buf, sizeof(buf));
        if (nr < (ssize_t)sizeof(buf))
            for (int i = (nr < 0 ? 0 : (int)nr); i < (int)sizeof(buf); i++)
                buf[i] = (unsigned char)rand();
        close(fd);
    } else {
        for (int i = 0; i < (int)sizeof(buf); i++) buf[i] = (unsigned char)rand();
    }
    for (int i = 0; i < (int)(sizeof(buf)); i++) {
        out[i*2]   = hex[buf[i] >> 4];
        out[i*2+1] = hex[buf[i] & 0xf];
    }
    out[WOLC_TOKEN_LEN] = '\0';
}

static struct wolc_session *wolc_session_find_locked(const char *token)
{
    for (int i = 0; i < WOLC_MAX_SESSIONS; i++)
        if (wolc_sessions[i].in_use &&
            strncmp(wolc_sessions[i].token, token, WOLC_TOKEN_LEN) == 0)
            return &wolc_sessions[i];
    return NULL;
}

static struct wolc_session *wolc_session_create_locked(int pfilepos, int level, const char *name)
{
    struct wolc_session *s = NULL;
    for (int i = 0; i < WOLC_MAX_SESSIONS; i++)
        if (!wolc_sessions[i].in_use) { s = &wolc_sessions[i]; break; }
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    wolc_gen_token(s->token);
    s->pfilepos = pfilepos;
    s->level    = level;
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->last_used = time(NULL);
    s->in_use    = 1;
    return s;
}

static void wolc_session_expire(void)
{
    time_t now = time(NULL);
    pthread_mutex_lock(&wolc_session_mutex);
    for (int i = 0; i < WOLC_MAX_SESSIONS; i++)
        if (wolc_sessions[i].in_use &&
            (now - wolc_sessions[i].last_used) > WOLC_SESSION_TTL)
            wolc_sessions[i].in_use = 0;
    pthread_mutex_unlock(&wolc_session_mutex);
}

/* ======================================================================
 * Permission check
 * ====================================================================== */

static int wolc_check_perm(int pfilepos, int level, int vnum)
{
    if (level >= LVL_GRGOD) return 1;
    int rnum = olc_vnum_to_zone_rnum(vnum);
    if (rnum < 0) return 0;
    struct olc_permissions_s *p = &zone_table[rnum].permissions;
    if (!(p->flags & OLC_ZONEFLAGS_CLOSED)) return 0;
    for (int i = 0; i < OLC_ZONE_MAX_AUTHORS; i++)
        if (p->authors[i] == pfilepos || p->editors[i] == pfilepos) return 1;
    return 0;
}

/* ======================================================================
 * Game-loop handlers (called ONLY from wolc_process_requests)
 * ====================================================================== */

static void wolc_handle_auth(struct wolc_request *req)
{
    struct char_file_u cbuf;
    long pi = get_ptable_by_name(req->auth_name);
    if (pi < 0) { req->status = WOLC_ERR_NOUSER; return; }
    if (load_char(req->auth_name, &cbuf) < 0) { req->status = WOLC_ERR_NOUSER; return; }
    if (strncmp(CRYPT(req->auth_pw, cbuf.pwd), cbuf.pwd, MAX_PWD_LENGTH) != 0) {
        req->status = WOLC_ERR_BADPW; return;
    }
    if (cbuf.level < LVL_IMMORT) { req->status = WOLC_ERR_NOPERM; return; }
    req->out_pfilepos = (int)pi;
    req->out_level    = (int)(unsigned char)cbuf.level;
    strncpy(req->out_name, cbuf.name, MAX_NAME_LENGTH);
    req->out_name[MAX_NAME_LENGTH] = '\0';
    req->status = WOLC_OK;
}

static void wolc_handle_get_zones(struct wolc_request *req)
{
    struct jbuf b;
    jb_init(&b, 8192);
    jb_append(&b, "[");
    for (int i = 0; i <= top_of_zone_table; i++) {
        if (i > 0) jb_append(&b, ",");
        int can_edit = wolc_check_perm(req->pfilepos, req->level, zone_table[i].bot);
        jb_append(&b, "{");
        jb_appendf(&b, "\"number\":%d,", zone_table[i].number);
        jb_append(&b, "\"name\":"); jb_escape_str(&b, zone_table[i].name);
        jb_appendf(&b, ",\"bot\":%d,\"top\":%d", zone_table[i].bot, zone_table[i].top);
        jb_appendf(&b, ",\"lifespan\":%d,\"reset_mode\":%d",
                   zone_table[i].lifespan, zone_table[i].reset_mode);
        jb_appendf(&b, ",\"closed\":%d,\"can_edit\":%d",
                   (zone_table[i].permissions.flags & OLC_ZONEFLAGS_CLOSED) ? 1 : 0,
                   can_edit ? 1 : 0);
        jb_append(&b, "}");
    }
    jb_append(&b, "]");
    req->response_json = b.data;
    req->status = WOLC_OK;
}

static void wolc_handle_get_room(struct wolc_request *req)
{
    room_rnum rnum = real_room(req->vnum);
    if (rnum == NOWHERE) { req->status = WOLC_ERR_NOTFOUND; return; }
    if (!wolc_check_perm(req->pfilepos, req->level, req->vnum)) {
        req->status = WOLC_ERR_NOPERM; return;
    }
    struct room_data *rm = &world[rnum];
    struct jbuf b;
    jb_init(&b, 4096);
    jb_append(&b, "{");
    jb_appendf(&b, "\"vnum\":%d,", req->vnum);
    jb_append(&b, "\"name\":"); jb_escape_str(&b, rm->name);
    jb_append(&b, ",\"description\":"); jb_escape_str(&b, rm->description);
    jb_appendf(&b, ",\"room_flags\":%d,\"sector_type\":%d", rm->room_flags, rm->sector_type);
    jb_append(&b, ",\"exits\":[");
    for (int d = 0; d < NUM_OF_DIRS; d++) {
        if (d > 0) jb_append(&b, ",");
        struct room_direction_data *ex = rm->dir_option[d];
        if (!ex) {
            jb_append(&b, "null");
        } else {
            int to_vnum = (ex->to_room != NOWHERE) ? (int)world[ex->to_room].number : -1;
            int key_vnum = (ex->key == NOTHING) ? -1 : (int)ex->key;
            jb_append(&b, "{\"general_description\":");
            jb_escape_str(&b, ex->general_description);
            jb_append(&b, ",\"keyword\":"); jb_escape_str(&b, ex->keyword);
            jb_appendf(&b, ",\"exit_info\":%d,\"key\":%d,\"to_room\":%d",
                       ex->exit_info, key_vnum, to_vnum);
            jb_append(&b, "}");
        }
    }
    jb_append(&b, "]");
    jb_append(&b, ",\"extra_descs\":"); jb_extra_descs(&b, rm->ex_description);
    jb_append(&b, "}");
    req->response_json = b.data;
    req->status = WOLC_OK;
}

static void wolc_handle_set_room(struct wolc_request *req)
{
    room_rnum rnum = real_room(req->vnum);
    if (rnum == NOWHERE) { req->status = WOLC_ERR_NOTFOUND; return; }
    if (!wolc_check_perm(req->pfilepos, req->level, req->vnum)) {
        req->status = WOLC_ERR_NOPERM; return;
    }
    const char *j = req->json_body;
    struct room_data *rm = &world[rnum];
    int ival;
    char *s;

    if ((s = json_get_str(j, "name")))        { free(rm->name);        rm->name        = s; }
    if ((s = json_get_str(j, "description"))) { free(rm->description); rm->description = s; }
    if (json_get_int(j, "room_flags",  &ival)) rm->room_flags  = ival;
    if (json_get_int(j, "sector_type", &ival)) rm->sector_type = ival;

    /* exits */
    const char *exits_arr = json_get_array(j, "exits");
    if (exits_arr) {
        exits_arr++;
        for (int d = 0; d < NUM_OF_DIRS; d++) {
            exits_arr = jp_skip(exits_arr);
            if (!*exits_arr || *exits_arr == ']') break;
            if (strncmp(exits_arr, "null", 4) == 0) {
                if (rm->dir_option[d]) {
                    free(rm->dir_option[d]->general_description);
                    free(rm->dir_option[d]->keyword);
                    free(rm->dir_option[d]);
                    rm->dir_option[d] = NULL;
                }
                exits_arr += 4;
            } else if (*exits_arr == '{') {
                char *obj = jp_extract_object(&exits_arr);
                char *gdesc = json_get_str(obj, "general_description");
                char *kw    = json_get_str(obj, "keyword");
                int ei = 0, key_vnum = -1, to_vnum = -1;
                json_get_int(obj, "exit_info", &ei);
                json_get_int(obj, "key",       &key_vnum);
                json_get_int(obj, "to_room",   &to_vnum);
                free(obj);

                if (!rm->dir_option[d]) {
                    rm->dir_option[d] = calloc(1, sizeof(struct room_direction_data));
                } else {
                    free(rm->dir_option[d]->general_description);
                    free(rm->dir_option[d]->keyword);
                    rm->dir_option[d]->general_description = NULL;
                    rm->dir_option[d]->keyword = NULL;
                }
                rm->dir_option[d]->general_description = gdesc;
                rm->dir_option[d]->keyword  = kw;
                rm->dir_option[d]->exit_info = ei;
                rm->dir_option[d]->key = (key_vnum >= 0) ? (obj_vnum)key_vnum : NOTHING;
                rm->dir_option[d]->to_room = (to_vnum >= 0) ? real_room(to_vnum) : NOWHERE;
            }
            exits_arr = jp_skip(exits_arr);
            if (*exits_arr == ',') exits_arr++;
        }
    }

    /* extra descs */
    if (json_get_array(j, "extra_descs")) {
        free_extra_descriptions(rm->ex_description);
        rm->ex_description = parse_extra_descs(j);
    }

    olc_save_room(req->vnum);
    req->response_json = strdup("{\"ok\":true}");
    req->status = WOLC_OK;
}

static void wolc_handle_get_mob(struct wolc_request *req)
{
    mob_rnum rnum = real_mobile(req->vnum);
    if (rnum == NOBODY) { req->status = WOLC_ERR_NOTFOUND; return; }
    if (!wolc_check_perm(req->pfilepos, req->level, req->vnum)) {
        req->status = WOLC_ERR_NOPERM; return;
    }
    struct char_data *mob = &mob_proto[rnum];
    struct jbuf b;
    jb_init(&b, 2048);
    jb_appendf(&b, "{\"vnum\":%d,", req->vnum);
    jb_append(&b, "\"aliases\":"); jb_escape_str(&b, mob->player.name);
    jb_append(&b, ",\"short_desc\":"); jb_escape_str(&b, mob->player.short_descr);
    jb_append(&b, ",\"long_desc\":"); jb_escape_str(&b, mob->player.long_descr);
    jb_append(&b, ",\"description\":"); jb_escape_str(&b, mob->player.description);
    jb_appendf(&b, ",\"act_flags\":%ld,\"aff_flags\":%ld",
               MOB_FLAGS(mob), AFF_FLAGS(mob));
    jb_appendf(&b, ",\"alignment\":%d,\"level\":%d",
               GET_ALIGNMENT(mob), (int)(unsigned char)mob->player.level);
    jb_appendf(&b, ",\"hitroll\":%d,\"ac\":%d",
               (int)mob->points.hitroll, (int)mob->points.armor);
    jb_appendf(&b, ",\"hp_nodice\":%d,\"hp_sizedice\":%d,\"hp_extra\":%d",
               mob->mob_specials.hpnodice, mob->mob_specials.hpsizedice,
               mob->mob_specials.hpextra);
    jb_appendf(&b, ",\"dam_nodice\":%d,\"dam_sizedice\":%d",
               mob->mob_specials.damnodice, mob->mob_specials.damsizedice);
    jb_appendf(&b, ",\"gold\":%d,\"exp\":%d", mob->points.gold, mob->points.exp);
    jb_appendf(&b, ",\"position\":%d,\"default_pos\":%d",
               mob->char_specials.position, mob->mob_specials.default_pos);
    jb_appendf(&b, ",\"sex\":%d,\"attack_type\":%d",
               mob->player.sex, mob->mob_specials.attack_type);
    jb_appendf(&b, ",\"str\":%d,\"str_add\":%d,\"intel\":%d,\"wis\":%d"
               ",\"dex\":%d,\"con\":%d,\"cha\":%d",
               (int)mob->real_abils.str, (int)mob->real_abils.str_add,
               (int)mob->real_abils.intel, (int)mob->real_abils.wis,
               (int)mob->real_abils.dex, (int)mob->real_abils.con,
               (int)mob->real_abils.cha);
    jb_append(&b, "}");
    req->response_json = b.data;
    req->status = WOLC_OK;
}

static void wolc_handle_set_mob(struct wolc_request *req)
{
    mob_rnum rnum = real_mobile(req->vnum);
    if (rnum == NOBODY) { req->status = WOLC_ERR_NOTFOUND; return; }
    if (!wolc_check_perm(req->pfilepos, req->level, req->vnum)) {
        req->status = WOLC_ERR_NOPERM; return;
    }
    const char *j = req->json_body;
    struct char_data *mob = &mob_proto[rnum];
    int ival;
    char *s;

    if ((s = json_get_str(j, "aliases")))    { free(mob->player.name);        mob->player.name        = s; }
    if ((s = json_get_str(j, "short_desc"))) { free(mob->player.short_descr); mob->player.short_descr = s; }
    if ((s = json_get_str(j, "long_desc")))  { free(mob->player.long_descr);  mob->player.long_descr  = s; }
    if ((s = json_get_str(j, "description"))){ free(mob->player.description); mob->player.description = s; }

    if (json_get_int(j, "act_flags",   &ival)) MOB_FLAGS(mob) = (long)ival;
    if (json_get_int(j, "aff_flags",   &ival)) AFF_FLAGS(mob) = (long)ival;
    if (json_get_int(j, "alignment",   &ival)) GET_ALIGNMENT(mob) = ival;
    if (json_get_int(j, "level",       &ival)) mob->player.level = (byte)ival;
    if (json_get_int(j, "hitroll",     &ival)) mob->points.hitroll = (sbyte)ival;
    if (json_get_int(j, "ac",          &ival)) mob->points.armor = (sh_int)ival;
    if (json_get_int(j, "hp_nodice",   &ival)) mob->mob_specials.hpnodice   = ival;
    if (json_get_int(j, "hp_sizedice", &ival)) mob->mob_specials.hpsizedice = ival;
    if (json_get_int(j, "hp_extra",    &ival)) mob->mob_specials.hpextra    = ival;
    if (json_get_int(j, "dam_nodice",  &ival)) mob->mob_specials.damnodice  = ival;
    if (json_get_int(j, "dam_sizedice",&ival)) mob->mob_specials.damsizedice= ival;
    if (json_get_int(j, "gold",        &ival)) mob->points.gold = ival;
    if (json_get_int(j, "exp",         &ival)) mob->points.exp  = ival;
    if (json_get_int(j, "position",    &ival)) mob->char_specials.position    = ival;
    if (json_get_int(j, "default_pos", &ival)) mob->mob_specials.default_pos  = ival;
    if (json_get_int(j, "sex",         &ival)) mob->player.sex                = ival;
    if (json_get_int(j, "attack_type", &ival)) mob->mob_specials.attack_type  = ival;
    if (json_get_int(j, "str",         &ival)) mob->real_abils.str     = (sbyte)ival;
    if (json_get_int(j, "str_add",     &ival)) mob->real_abils.str_add = (sbyte)ival;
    if (json_get_int(j, "intel",       &ival)) mob->real_abils.intel   = (sbyte)ival;
    if (json_get_int(j, "wis",         &ival)) mob->real_abils.wis     = (sbyte)ival;
    if (json_get_int(j, "dex",         &ival)) mob->real_abils.dex     = (sbyte)ival;
    if (json_get_int(j, "con",         &ival)) mob->real_abils.con     = (sbyte)ival;
    if (json_get_int(j, "cha",         &ival)) mob->real_abils.cha     = (sbyte)ival;

    olc_save_mobile(req->vnum);
    req->response_json = strdup("{\"ok\":true}");
    req->status = WOLC_OK;
}

static void wolc_handle_get_obj(struct wolc_request *req)
{
    obj_rnum rnum = real_object(req->vnum);
    if (rnum == NOBODY) { req->status = WOLC_ERR_NOTFOUND; return; }
    if (!wolc_check_perm(req->pfilepos, req->level, req->vnum)) {
        req->status = WOLC_ERR_NOPERM; return;
    }
    struct obj_data *obj = &obj_proto[rnum];
    struct jbuf b;
    jb_init(&b, 2048);
    jb_appendf(&b, "{\"vnum\":%d,", req->vnum);
    jb_append(&b, "\"aliases\":"); jb_escape_str(&b, obj->name);
    jb_append(&b, ",\"room_desc\":"); jb_escape_str(&b, obj->description);
    jb_append(&b, ",\"short_desc\":"); jb_escape_str(&b, obj->short_description);
    jb_append(&b, ",\"action_desc\":"); jb_escape_str(&b, obj->action_description);
    jb_appendf(&b, ",\"type\":%d,\"extra_flags\":%d,\"wear_flags\":%d",
               GET_OBJ_TYPE(obj), obj->obj_flags.extra_flags, GET_OBJ_WEAR(obj));
    jb_appendf(&b, ",\"weight\":%d,\"cost\":%d,\"rent\":%d",
               GET_OBJ_WEIGHT(obj), GET_OBJ_COST(obj), GET_OBJ_RENT(obj));
    jb_appendf(&b, ",\"val0\":%d,\"val1\":%d,\"val2\":%d,\"val3\":%d",
               GET_OBJ_VAL(obj,0), GET_OBJ_VAL(obj,1),
               GET_OBJ_VAL(obj,2), GET_OBJ_VAL(obj,3));
    jb_append(&b, ",\"affects\":[");
    for (int i = 0; i < MAX_OBJ_AFFECT; i++) {
        if (i > 0) jb_append(&b, ",");
        jb_appendf(&b, "{\"location\":%d,\"modifier\":%d}",
                   (int)obj->affected[i].location,
                   (int)(sbyte)obj->affected[i].modifier);
    }
    jb_append(&b, "]");
    jb_append(&b, ",\"extra_descs\":"); jb_extra_descs(&b, obj->ex_description);
    jb_append(&b, "}");
    req->response_json = b.data;
    req->status = WOLC_OK;
}

static void wolc_handle_set_obj(struct wolc_request *req)
{
    obj_rnum rnum = real_object(req->vnum);
    if (rnum == NOBODY) { req->status = WOLC_ERR_NOTFOUND; return; }
    if (!wolc_check_perm(req->pfilepos, req->level, req->vnum)) {
        req->status = WOLC_ERR_NOPERM; return;
    }
    const char *j = req->json_body;
    struct obj_data *obj = &obj_proto[rnum];
    int ival;
    char *s;

    if ((s = json_get_str(j, "aliases")))    { free(obj->name);               obj->name               = s; }
    if ((s = json_get_str(j, "room_desc")))  { free(obj->description);        obj->description        = s; }
    if ((s = json_get_str(j, "short_desc"))) { free(obj->short_description);  obj->short_description  = s; }
    if ((s = json_get_str(j, "action_desc"))){ free(obj->action_description); obj->action_description = s; }

    if (json_get_int(j, "type",        &ival)) obj->obj_flags.type_flag    = ival;
    if (json_get_int(j, "extra_flags", &ival)) obj->obj_flags.extra_flags  = ival;
    if (json_get_int(j, "wear_flags",  &ival)) obj->obj_flags.wear_flags   = ival;
    if (json_get_int(j, "weight",      &ival)) obj->obj_flags.weight       = ival;
    if (json_get_int(j, "cost",        &ival)) obj->obj_flags.cost         = ival;
    if (json_get_int(j, "rent",        &ival)) obj->obj_flags.cost_per_day = ival;
    if (json_get_int(j, "val0",        &ival)) obj->obj_flags.value[0]     = ival;
    if (json_get_int(j, "val1",        &ival)) obj->obj_flags.value[1]     = ival;
    if (json_get_int(j, "val2",        &ival)) obj->obj_flags.value[2]     = ival;
    if (json_get_int(j, "val3",        &ival)) obj->obj_flags.value[3]     = ival;

    const char *aff_arr = json_get_array(j, "affects");
    if (aff_arr) {
        aff_arr++;
        for (int i = 0; i < MAX_OBJ_AFFECT; i++) {
            aff_arr = jp_skip(aff_arr);
            if (!*aff_arr || *aff_arr == ']') break;
            if (*aff_arr != '{') { aff_arr = jp_skip_value(aff_arr); i--; continue; }
            char *aobj = jp_extract_object(&aff_arr);
            int loc = 0, mod = 0;
            json_get_int(aobj, "location", &loc);
            json_get_int(aobj, "modifier", &mod);
            free(aobj);
            obj->affected[i].location = (byte)loc;
            obj->affected[i].modifier = (sbyte)mod;
            aff_arr = jp_skip(aff_arr);
            if (*aff_arr == ',') aff_arr++;
        }
    }

    if (json_get_array(j, "extra_descs")) {
        free_extra_descriptions(obj->ex_description);
        obj->ex_description = parse_extra_descs(j);
    }

    olc_save_object(req->vnum);
    req->response_json = strdup("{\"ok\":true}");
    req->status = WOLC_OK;
}

static void wolc_handle_get_zcmds(struct wolc_request *req)
{
    zone_rnum rnum = real_zone((zone_vnum)req->vnum);
    if (rnum == NOWHERE) { req->status = WOLC_ERR_NOTFOUND; return; }
    if (!wolc_check_perm(req->pfilepos, req->level, zone_table[rnum].bot)) {
        req->status = WOLC_ERR_NOPERM; return;
    }
    struct zone_data *z = &zone_table[rnum];
    struct jbuf b;
    jb_init(&b, 4096);
    jb_append(&b, "{\"name\":"); jb_escape_str(&b, z->name);
    jb_appendf(&b, ",\"number\":%d,\"bot\":%d,\"top\":%d", z->number, z->bot, z->top);
    jb_appendf(&b, ",\"lifespan\":%d,\"reset_mode\":%d", z->lifespan, z->reset_mode);
    jb_append(&b, ",\"commands\":[");
    int first = 1;
    if (z->cmd) {
        for (int i = 0; z->cmd[i].command != 'S'; i++) {
            if (!first) jb_append(&b, ",");
            first = 0;
            struct reset_com *c = &z->cmd[i];
            int a1 = c->arg1, a2 = c->arg2, a3 = c->arg3;
            switch (c->command) {
                case 'M':
                    if (c->arg1 >= 0 && c->arg1 <= top_of_mobt)
                        a1 = mob_index[c->arg1].vnum;
                    a3 = (c->arg3 != (int)NOWHERE && c->arg3 >= 0) ?
                         (int)world[c->arg3].number : -1;
                    break;
                case 'O':
                    if (c->arg1 >= 0 && c->arg1 <= top_of_objt)
                        a1 = obj_index[c->arg1].vnum;
                    a3 = (c->arg3 != (int)NOWHERE && c->arg3 >= 0) ?
                         (int)world[c->arg3].number : -1;
                    break;
                case 'E': case 'G':
                    if (c->arg1 >= 0 && c->arg1 <= top_of_objt)
                        a1 = obj_index[c->arg1].vnum;
                    break;
                case 'P':
                    if (c->arg1 >= 0 && c->arg1 <= top_of_objt)
                        a1 = obj_index[c->arg1].vnum;
                    if (c->arg3 >= 0 && c->arg3 <= top_of_objt)
                        a3 = obj_index[c->arg3].vnum;
                    break;
                case 'D':
                    if (c->arg1 >= 0 && c->arg1 <= top_of_world)
                        a1 = (int)world[c->arg1].number;
                    break;
                default: break;
            }
            jb_appendf(&b,
                "{\"command\":\"%c\",\"if_flag\":%d,"
                "\"arg1\":%d,\"arg2\":%d,\"arg3\":%d}",
                c->command, c->if_flag ? 1 : 0, a1, a2, a3);
        }
    }
    jb_append(&b, "]}");
    req->response_json = b.data;
    req->status = WOLC_OK;
}

static void wolc_handle_set_zcmds(struct wolc_request *req)
{
    zone_rnum rnum = real_zone((zone_vnum)req->vnum);
    if (rnum == NOWHERE) { req->status = WOLC_ERR_NOTFOUND; return; }
    if (!wolc_check_perm(req->pfilepos, req->level, zone_table[rnum].bot)) {
        req->status = WOLC_ERR_NOPERM; return;
    }
    const char *j = req->json_body;
    const char *arr = json_get_array(j, "commands");
    if (!arr) { req->status = WOLC_ERR_INTERNAL; return; }
    arr++;

    /* First pass: count commands */
    int count = 0;
    const char *p = arr;
    while (*(p = jp_skip(p)) && *p != ']') {
        if (*p == '{') { count++; p = jp_skip_value(p); }
        else { p++; }
        p = jp_skip(p);
        if (*p == ',') p++;
    }

    struct reset_com *cmds = calloc(count + 1, sizeof(*cmds));
    if (!cmds) { req->status = WOLC_ERR_INTERNAL; return; }

    int idx = 0;
    const char *ap = arr;
    while (*(ap = jp_skip(ap)) && *ap != ']' && idx < count) {
        if (*ap != '{') { ap++; continue; }
        char *obj = jp_extract_object(&ap);
        char *cmd_str = json_get_str(obj, "command");
        int if_flag = 0, a1 = 0, a2 = 0, a3 = -1;
        json_get_int(obj, "if_flag", &if_flag);
        json_get_int(obj, "arg1",    &a1);
        json_get_int(obj, "arg2",    &a2);
        json_get_int(obj, "arg3",    &a3);
        free(obj);

        char cmd_c = (cmd_str && cmd_str[0]) ? cmd_str[0] : 'S';
        free(cmd_str);

        cmds[idx].command = cmd_c;
        cmds[idx].if_flag = if_flag ? TRUE : FALSE;
        cmds[idx].line    = 0;
        switch (cmd_c) {
            case 'M':
                cmds[idx].arg1 = (int)real_mobile((mob_vnum)a1);
                cmds[idx].arg2 = a2;
                cmds[idx].arg3 = (a3 >= 0) ? (int)real_room((room_vnum)a3) : (int)NOWHERE;
                break;
            case 'O':
                cmds[idx].arg1 = (int)real_object((obj_vnum)a1);
                cmds[idx].arg2 = a2;
                cmds[idx].arg3 = (a3 >= 0) ? (int)real_room((room_vnum)a3) : (int)NOWHERE;
                break;
            case 'E': case 'G':
                cmds[idx].arg1 = (int)real_object((obj_vnum)a1);
                cmds[idx].arg2 = a2;
                cmds[idx].arg3 = a3;
                break;
            case 'P':
                cmds[idx].arg1 = (int)real_object((obj_vnum)a1);
                cmds[idx].arg2 = a2;
                cmds[idx].arg3 = (int)real_object((obj_vnum)a3);
                break;
            case 'D':
                cmds[idx].arg1 = (int)real_room((room_vnum)a1);
                cmds[idx].arg2 = a2;
                cmds[idx].arg3 = a3;
                break;
            default:
                cmds[idx].arg1 = a1; cmds[idx].arg2 = a2; cmds[idx].arg3 = a3;
                break;
        }
        idx++;
        ap = jp_skip(ap);
        if (*ap == ',') ap++;
    }
    cmds[idx].command = 'S';

    free(zone_table[rnum].cmd);
    zone_table[rnum].cmd = cmds;
    zedit_save_zone_file(rnum);
    req->response_json = strdup("{\"ok\":true}");
    req->status = WOLC_OK;
}

/* ======================================================================
 * Process queue (called ONLY from game loop thread)
 * ====================================================================== */

void wolc_process_requests(void)
{
    pthread_mutex_lock(&wolc_queue_mutex);
    struct wolc_request *q = wolc_queue_head;
    wolc_queue_head = wolc_queue_tail = NULL;
    pthread_mutex_unlock(&wolc_queue_mutex);

    while (q) {
        struct wolc_request *next = q->next;
        switch (q->type) {
            case WOLC_REQ_AUTH:      wolc_handle_auth(q);      break;
            case WOLC_REQ_GET_ZONES: wolc_handle_get_zones(q); break;
            case WOLC_REQ_GET_ROOM:  wolc_handle_get_room(q);  break;
            case WOLC_REQ_SET_ROOM:  wolc_handle_set_room(q);  break;
            case WOLC_REQ_GET_MOB:   wolc_handle_get_mob(q);   break;
            case WOLC_REQ_SET_MOB:   wolc_handle_set_mob(q);   break;
            case WOLC_REQ_GET_OBJ:   wolc_handle_get_obj(q);   break;
            case WOLC_REQ_SET_OBJ:   wolc_handle_set_obj(q);   break;
            case WOLC_REQ_GET_ZCMDS: wolc_handle_get_zcmds(q); break;
            case WOLC_REQ_SET_ZCMDS: wolc_handle_set_zcmds(q); break;
            default: q->status = WOLC_ERR_INTERNAL; break;
        }
        pthread_mutex_lock(&q->lock);
        q->done = 1;
        pthread_cond_signal(&q->done_cond);
        pthread_mutex_unlock(&q->lock);
        q = next;
    }
}

/* ======================================================================
 * HTTP helpers
 * ====================================================================== */

static int wolc_get_token(struct mg_connection *conn, char *out, int out_len)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);
    for (int i = 0; i < ri->num_headers; i++) {
        if (strcasecmp(ri->http_headers[i].name, "Authorization") == 0) {
            const char *v = ri->http_headers[i].value;
            if (strncasecmp(v, "Bearer ", 7) == 0) {
                strncpy(out, v + 7, out_len - 1);
                out[out_len - 1] = '\0';
                return 1;
            }
        }
    }
    return 0;
}

static int wolc_validate_session(struct mg_connection *conn,
                                  int *pfilepos, int *level)
{
    char token[WOLC_TOKEN_LEN + 4];
    if (!wolc_get_token(conn, token, sizeof(token))) return 0;
    pthread_mutex_lock(&wolc_session_mutex);
    struct wolc_session *s = wolc_session_find_locked(token);
    if (s) {
        s->last_used = time(NULL);
        *pfilepos = s->pfilepos;
        *level    = s->level;
    }
    pthread_mutex_unlock(&wolc_session_mutex);
    return s != NULL;
}

static void wolc_send_json(struct mg_connection *conn, int code,
                            const char *status_text, const char *json)
{
    int len = json ? (int)strlen(json) : 0;
    mg_printf(conn,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Cache-Control: no-cache\r\n"
        "\r\n", code, status_text, len);
    if (len > 0) mg_write(conn, json, len);
}

static void wolc_send_error(struct mg_connection *conn, int code,
                             const char *status_text, const char *msg)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg ? msg : status_text);
    wolc_send_json(conn, code, status_text, buf);
}

static int wolc_parse_vnum(const char *uri, const char *prefix)
{
    int plen = (int)strlen(prefix);
    if (strncmp(uri, prefix, plen) != 0) return -1;
    const char *p = uri + plen;
    while (*p == '/') p++;
    if (!*p) return -1;
    char *end;
    long v = strtol(p, &end, 10);
    if (end == p) return -1;
    return (int)v;
}

/* Helper: submit request, wait for result, return status.
 * On timeout or OOM, frees req and sends an error response. Returns 0 on success. */
static int wolc_submit_and_wait(struct mg_connection *conn, struct wolc_request *r)
{
    wolc_enqueue(r);
    if (wolc_wait(r) != 0) {
        wolc_request_free(r);
        wolc_send_error(conn, 503, "Service Unavailable", "Game loop timeout");
        return -1;
    }
    return 0;
}

/* ======================================================================
 * HTTP handlers
 * ====================================================================== */

static int wolc_http_login(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);
    if (strcmp(ri->request_method, "POST") != 0) {
        wolc_send_error(conn, 405, "Method Not Allowed", "Use POST"); return 1;
    }
    char body[2048];
    int len = mg_read(conn, body, (int)sizeof(body) - 1);
    if (len <= 0) { wolc_send_error(conn, 400, "Bad Request", "Empty body"); return 1; }
    body[len] = '\0';

    char *name = json_get_str(body, "name");
    char *pw   = json_get_str(body, "password");
    if (!name || !pw) {
        free(name); free(pw);
        wolc_send_error(conn, 400, "Bad Request", "Missing name or password");
        return 1;
    }

    struct wolc_request *r = wolc_request_alloc(WOLC_REQ_AUTH);
    if (!r) { free(name); free(pw); wolc_send_error(conn, 503, "Service Unavailable", "OOM"); return 1; }
    strncpy(r->auth_name, name, MAX_NAME_LENGTH);
    strncpy(r->auth_pw,   pw,   MAX_INPUT_LENGTH - 1);
    free(name); free(pw);

    if (wolc_submit_and_wait(conn, r) != 0) return 1;

    if (r->status != WOLC_OK) {
        const char *msg;
        switch (r->status) {
            case WOLC_ERR_NOUSER: msg = "Unknown player";    break;
            case WOLC_ERR_BADPW:  msg = "Bad password";      break;
            case WOLC_ERR_NOPERM: msg = "Not an immortal";   break;
            default:              msg = "Authentication failed"; break;
        }
        wolc_request_free(r);
        wolc_send_error(conn, 401, "Unauthorized", msg);
        return 1;
    }

    int pfilepos = r->out_pfilepos, level = r->out_level;
    char pname[MAX_NAME_LENGTH + 1];
    strncpy(pname, r->out_name, MAX_NAME_LENGTH);
    pname[MAX_NAME_LENGTH] = '\0';
    wolc_request_free(r);

    pthread_mutex_lock(&wolc_session_mutex);
    struct wolc_session *s = wolc_session_create_locked(pfilepos, level, pname);
    char tok[WOLC_TOKEN_LEN + 1] = "";
    if (s) strncpy(tok, s->token, WOLC_TOKEN_LEN);
    pthread_mutex_unlock(&wolc_session_mutex);

    if (!s) { wolc_send_error(conn, 503, "Service Unavailable", "Session table full"); return 1; }

    char resp[512];
    snprintf(resp, sizeof(resp),
             "{\"token\":\"%s\",\"name\":\"%s\",\"level\":%d}", tok, pname, level);
    wolc_send_json(conn, 200, "OK", resp);
    return 1;
}

static int wolc_http_logout(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    char token[WOLC_TOKEN_LEN + 4];
    if (wolc_get_token(conn, token, sizeof(token))) {
        pthread_mutex_lock(&wolc_session_mutex);
        struct wolc_session *s = wolc_session_find_locked(token);
        if (s) s->in_use = 0;
        pthread_mutex_unlock(&wolc_session_mutex);
    }
    wolc_send_json(conn, 200, "OK", "{\"ok\":true}");
    return 1;
}

static int wolc_http_zones(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    int pfilepos, level;
    if (!wolc_validate_session(conn, &pfilepos, &level)) {
        wolc_send_error(conn, 401, "Unauthorized", "Invalid or expired session"); return 1;
    }
    struct wolc_request *r = wolc_request_alloc(WOLC_REQ_GET_ZONES);
    if (!r) { wolc_send_error(conn, 503, "Service Unavailable", "OOM"); return 1; }
    r->pfilepos = pfilepos; r->level = level;
    if (wolc_submit_and_wait(conn, r) != 0) return 1;
    if (r->status != WOLC_OK) {
        wolc_request_free(r);
        wolc_send_error(conn, 500, "Internal Server Error", "Failed");
        return 1;
    }
    wolc_send_json(conn, 200, "OK", r->response_json);
    wolc_request_free(r);
    return 1;
}

/* Generic GET/POST handler for room, mob, obj, zone commands */
static int wolc_http_entity(struct mg_connection *conn,
                             const char *prefix,
                             int get_type, int set_type,
                             int body_size)
{
    int pfilepos, level;
    if (!wolc_validate_session(conn, &pfilepos, &level)) {
        wolc_send_error(conn, 401, "Unauthorized", "Invalid or expired session"); return 1;
    }
    const struct mg_request_info *ri = mg_get_request_info(conn);
    int vnum = wolc_parse_vnum(ri->local_uri, prefix);
    if (vnum < 0) { wolc_send_error(conn, 400, "Bad Request", "Invalid vnum/number"); return 1; }

    int is_post = (strcmp(ri->request_method, "POST") == 0);
    struct wolc_request *r = wolc_request_alloc(is_post ? set_type : get_type);
    if (!r) { wolc_send_error(conn, 503, "Service Unavailable", "OOM"); return 1; }
    r->pfilepos = pfilepos; r->level = level; r->vnum = vnum;

    if (is_post) {
        char *body = malloc(body_size + 1);
        if (!body) {
            wolc_request_free(r);
            wolc_send_error(conn, 503, "Service Unavailable", "OOM");
            return 1;
        }
        int n = mg_read(conn, body, body_size);
        if (n < 0) n = 0;
        body[n] = '\0';
        r->json_body = body;
    }

    if (wolc_submit_and_wait(conn, r) != 0) return 1;

    if (r->status == WOLC_ERR_NOTFOUND) {
        wolc_request_free(r); wolc_send_error(conn, 404, "Not Found",  "Vnum not found"); return 1;
    }
    if (r->status == WOLC_ERR_NOPERM) {
        wolc_request_free(r); wolc_send_error(conn, 403, "Forbidden",  "No edit permission"); return 1;
    }
    if (r->status != WOLC_OK) {
        wolc_request_free(r); wolc_send_error(conn, 500, "Internal Server Error", "Failed"); return 1;
    }
    wolc_send_json(conn, 200, "OK", r->response_json);
    wolc_request_free(r);
    return 1;
}

static int wolc_http_room(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    return wolc_http_entity(conn, "/olc/room/",
                            WOLC_REQ_GET_ROOM, WOLC_REQ_SET_ROOM, 65535);
}

static int wolc_http_mob(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    return wolc_http_entity(conn, "/olc/mob/",
                            WOLC_REQ_GET_MOB, WOLC_REQ_SET_MOB, 32767);
}

static int wolc_http_obj(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    return wolc_http_entity(conn, "/olc/obj/",
                            WOLC_REQ_GET_OBJ, WOLC_REQ_SET_OBJ, 32767);
}

static int wolc_http_zone(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    return wolc_http_entity(conn, "/olc/zone/",
                            WOLC_REQ_GET_ZCMDS, WOLC_REQ_SET_ZCMDS, 65535);
}

/* ======================================================================
 * Public API
 * ====================================================================== */

void wolc_register_handlers(struct mg_context *ctx)
{
    /* Do NOT register a catch-all /olc handler: civetweb stops at the first
     * prefix match that returns 0 and falls through to static file serving,
     * which would prevent /olc/room/<n> etc. from ever reaching their handlers.
     * Instead, rely on civetweb's built-in directory index serving for /olc/
     * (it auto-serves index.html). */
    mg_set_request_handler(ctx, "/olc/login",  wolc_http_login,  NULL);
    mg_set_request_handler(ctx, "/olc/logout", wolc_http_logout, NULL);
    mg_set_request_handler(ctx, "/olc/zones",  wolc_http_zones,  NULL);
    mg_set_request_handler(ctx, "/olc/room/",  wolc_http_room,   NULL);
    mg_set_request_handler(ctx, "/olc/mob/",   wolc_http_mob,    NULL);
    mg_set_request_handler(ctx, "/olc/obj/",   wolc_http_obj,    NULL);
    mg_set_request_handler(ctx, "/olc/zone/",  wolc_http_zone,   NULL);
}

void wolc_shutdown(void)
{
    pthread_mutex_lock(&wolc_session_mutex);
    memset(wolc_sessions, 0, sizeof(wolc_sessions));
    pthread_mutex_unlock(&wolc_session_mutex);
}

#endif /* HAVE_CIVETWEB */

void webserver_olc_heartbeat(void)
{
#ifdef HAVE_CIVETWEB
    static int tick = 0;
    wolc_process_requests();
    if (!(++tick % 600))   /* ~once per minute at 10 pulses/sec */
        wolc_session_expire();
#endif
}
