#include "utils.h"
#include "net_utils.h"

#ifndef MIMUW_SIK_ZAD1_UI_H
#define MIMUW_SIK_ZAD1_UI_H

#define MAX_CONNS 10 // Maximum number of clients using the UI.

void* run_ui(void*);

inline bool is_up_arrow(const char* key, ssize_t n_bytes) {
    if (n_bytes < 4) // TODO: change to != 4 after no enter
        return false;
    return key[0] == 0x1b && key[1] == 0x5b && key[2] == 0x41 && key[3] == 0xd;
}

inline bool is_down_arrow(const char* key, int n_bytes) {
    if (n_bytes < 4) // TODO: change to != 4 after no enter
        return false;
    return key[0] == 0x1b && key[1] == 0x5b && key[2] == 0x42 && key[3] == 0xd;
}

#endif //MIMUW_SIK_ZAD1_UI_H
