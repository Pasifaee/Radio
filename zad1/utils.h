#include <cstdint>
#include <string>
#include <iostream>
#include <stdexcept>
#include <boost/program_options.hpp>
#include "net_utils.h"

#ifndef MIMUW_SIK_ZAD1_UTILS_H
#define MIMUW_SIK_ZAD1_UTILS_H

// Default values.
#define DFLT_PORT 27924
#define DFLT_PSIZE 512
#define DFLT_BSIZE 65536
#define DFLT_NAME "Nienazwany nadajnik"

// Constants.
#define NO_FLAGS 0

struct audio_pack {
    uint64_t session_id;
    uint64_t first_byte_num;
    byte_t* audio_data;

    size_t PSIZE; // Size of audio_data.
};

/**
 * TODO
 */
void get_options(bool sender, int ac, char* av[], addr_t* address, port_t* port, size_t* bsize, size_t* psize = nullptr, std::string* name = nullptr);

inline void print_bytes(byte_t* bytes, size_t n, char* message = nullptr) {
    if (message)
        std::cout << message << "\n";
    for (size_t i = 0; i < n; i++) {
        std::cout << std::hex << (unsigned int) bytes[i] << " ";
    }
    std::cout << "\n";
}

#endif //MIMUW_SIK_ZAD1_UTILS_H
