#include "utils.h"

#ifndef MIMUW_SIK_ZAD1_SIKRADIO_RECEIVER_H
#define MIMUW_SIK_ZAD1_SIKRADIO_RECEIVER_H

audio_pack read_datagram(const byte_t* datagram, size_t package_size);

void init_connection(struct pollfd* poll_desc);

ssize_t receive_new_package(int socket_fd, byte_t* start_buffer);

void new_audio_session(audio_pack start_package, size_t new_PSIZE, byte_t* buffer);

void write_package_to_buffer(const audio_pack package, byte_t* buffer);

void handle_new_package(size_t package_size, const byte_t* datagram, byte_t* buffer);

void transmit_music();

#endif //MIMUW_SIK_ZAD1_SIKRADIO_RECEIVER_H
