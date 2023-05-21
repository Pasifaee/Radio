#include "utils.h"

#ifndef MIMUW_SIK_ZAD1_SIKRADIO_SENDER_H
#define MIMUW_SIK_ZAD1_SIKRADIO_SENDER_H

ssize_t read_music(byte_t* buff);

ssize_t fill_audio_datagram(byte_t* datagram, uint64_t session_id, uint64_t first_byte_num);

void read_and_send_music();

#endif //MIMUW_SIK_ZAD1_SIKRADIO_SENDER_H
