#ifndef _ORCHESTRATION_H_
#define _ORCHESTRATION_H_


/* Optional callback that happen before the system is going to sleep */
extern before_sleep *on_before_sleep_cb;

void update_next_availability_window();

bool ask_for_time(uint64_t ask);

void take_control();
void give_control(sdp_peer * peer); 

void sdp_orchestration_init(char * _log_prefix, before_sleep _on_before_sleep_cb); 

void sleep_until_peer_available(sdp_peer *peer, uint64_t margin_us);

//  Availability When/Next messaging

int sdp_orchestration_send_when_message(sdp_peer *peer);
int sdp_orchestration_send_next_message(work_queue_item_t *queue_item);
void sdp_orchestration_parse_next_message(work_queue_item_t *queue_item);

#endif