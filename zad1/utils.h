#include <cstdint>
#include <string>
#include <iostream>
#include <stdexcept>
#include <sys/time.h>
#include <boost/program_options.hpp>
#include "net_utils.h"

#ifndef MIMUW_SIK_ZAD1_UTILS_H
#define MIMUW_SIK_ZAD1_UTILS_H

// Default values.
#define DFLT_DISCOVER_ADDR "255.255.255.255"
#define DFLT_DATA_PORT 27924
#define DFLT_CTRL_PORT 37924
#define DFLT_PSIZE 512
#define DFLT_BSIZE 65536
#define DFLT_NAME "Nienazwany nadajnik"

// Constants.
#define NO_FLAGS 0
#define MSG_BUFF_SIZE 1024
#define LOOKUP_STR (std::string) "ZERO_SEVEN_COME_IN"
#define REPLY_STR (std::string) "BOREWICZ_HERE"
#define REXMIT_STR (std::string) "LOUDER_PLEASE"

struct audio_pack {
    uint64_t session_id;
    uint64_t first_byte_num;
    byte_t* audio_data;
};

enum message_type {LOOKUP = 0, REPLY = 1, REXMIT = 2, INCORRECT = 3};

struct message {
    message_type msg_type;

    // REPLY parameters.
    addr_t mcast_addr;
    port_t data_port;
    std::string name;

    // REXMIT parameters.
    std::vector<uint64_t> packages;
};

std::string get_message_str(message msg);

message parse_message(std::string msg_str);

struct timer {
    const long int base;
    long int remaining; // Remaining time in miliseconds.
    timeval last_time;
};

inline timer new_timer(long int base) {
    timeval now;
    gettimeofday(&now, nullptr);
    return timer {base, base, now};
}

inline void reset_timer(timer* timer) {
    timer->remaining = timer->base;
    gettimeofday(&timer->last_time, nullptr);
}

/**
 * Updates the timer and returns the check_time time.
 */
inline long int check_time(timer* timer) {
    timeval now;
    gettimeofday(&now, nullptr);

    long int time_diff = (now.tv_sec - timer->last_time.tv_sec) * 1000; // sec to ms
    time_diff += (now.tv_usec - timer->last_time.tv_usec) / 1000; // us to ms

    timer->remaining -= time_diff;
    timer->last_time = now;
    return timer->remaining;
}

/**
 * TODO - description
 */
void get_options(bool sender, int ac, char* av[], addr_t* address, port_t* data_port, port_t* ctrl_port, size_t* bsize, size_t* psize = nullptr, std::string* name = nullptr);

#endif //MIMUW_SIK_ZAD1_UTILS_H
