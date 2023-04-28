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

/**
 * TODO
 * @param sender
 * @param ac
 * @param av
 * @param address
 * @param port
 * @param bsize
 * @param psize
 * @param name
 */
void get_options(bool sender, int ac, char* av[], addr_t* address, port_t* port, size_t* bsize, size_t* psize = nullptr, std::string* name = nullptr);

#endif //MIMUW_SIK_ZAD1_UTILS_H
