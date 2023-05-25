#include <cstdint>
#include <string>
#include <iostream>
#include <stdexcept>
#include <boost/program_options.hpp>
#include "net_utils.h"

#ifndef MIMUW_SIK_ZAD1_UTILS_H
#define MIMUW_SIK_ZAD1_UTILS_H

// Default values.
#define DFLT_DATA_PORT 27924
#define DFLT_CTRL_PORT 37924
#define DFLT_PSIZE 512
#define DFLT_BSIZE 65536
#define DFLT_NAME "Nienazwany nadajnik"

// Constants.
#define NO_FLAGS 0
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

std::string create_message(message msg);

message parse_message(std::string msg_str);

/**
 * TODO - description
 */
void get_options(bool sender, int ac, char* av[], addr_t* address, port_t* data_port, port_t* ctrl_port, size_t* bsize, size_t* psize = nullptr, std::string* name = nullptr);

#endif //MIMUW_SIK_ZAD1_UTILS_H
