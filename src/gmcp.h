/* ************************************************************************
*   File: gmcp.h                                        Part of CircleMUD *
*  Usage: GMCP (Generic MUD Communication Protocol) support               *
************************************************************************ */

#ifndef __GMCP_H__
#define __GMCP_H__

#define TELOPT_GMCP  201   /* 0xC9 */

/* IAC parser states stored in descriptor_data.gmcp_iac_state */
#define IAC_NORMAL       0
#define IAC_GOT_IAC      1
#define IAC_GOT_CMD      2
#define IAC_SB_DATA      3
#define IAC_SB_GOT_IAC   4

/* Telnet negotiation */
void gmcp_send_will(struct descriptor_data *d);
void gmcp_negotiate(struct descriptor_data *d);
void gmcp_handle_sb(struct descriptor_data *d);

/* Called from process_input() to strip IAC sequences before line processing */
int  gmcp_strip_iac(struct descriptor_data *d, char *buf, int len);

/* Game state senders — all are no-ops if d->gmcp_enabled is false */
void gmcp_send_char_vitals(struct char_data *ch);
void gmcp_send_char_statusvars(struct char_data *ch);
void gmcp_send_char_status(struct char_data *ch);
void gmcp_send_room_info(struct char_data *ch);
void gmcp_send_char_items_list(struct char_data *ch);
void gmcp_send_char_items_add(struct char_data *ch, struct obj_data *obj);
void gmcp_send_char_items_remove(struct char_data *ch, struct obj_data *obj);
void gmcp_send_comm_channel(struct descriptor_data *d, const char *channel,
                             const char *talker, const char *text);

#endif /* __GMCP_H__ */
