#include "utils.h"
#include "net_utils.h"

#ifndef MIMUW_SIK_ZAD1_UI_H
#define MIMUW_SIK_ZAD1_UI_H

#define MAX_CONNS 10 // Maximum number of clients using the UI.
#define UPDATE_STR "UPDATE"
#define CHANGE_STR "CHANGE"

struct thread_args {
    port_t ui_port;
    int write_fd;
    int read_fd;
    std::map<sockaddr_in, radio_station>* radio_stations_ptr;
    pthread_mutex_t* lock_ptr;
    sockaddr_in* curr_station_ptr;
    sockaddr_in* change_station_ptr;
};

void* run_ui(void*);

inline bool is_up_arrow(const char* key, ssize_t n_bytes) {
    if (n_bytes != 3) // TODO: change to != 4 after no enter
        return false;
    return key[0] == 0x1b && key[1] == 0x5b && key[2] == 0x41;
}

inline bool is_down_arrow(const char* key, int n_bytes) {
    if (n_bytes != 3) // TODO: change to != 4 after no enter
        return false;
    return key[0] == 0x1b && key[1] == 0x5b && key[2] == 0x42;
}

#endif //MIMUW_SIK_ZAD1_UI_H
