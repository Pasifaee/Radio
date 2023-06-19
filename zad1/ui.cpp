#include <poll.h>
#include "ui.h"

typedef std::pair<std::string, sockaddr_in> station_print;

#define CENTRAL 0
#define RECV_IN 1

void turn_off_enter(pollfd* poll_desc, int client_nr) {
    const char* turn_off_message = "\xFF\xFD\x22";
    send_data(poll_desc[client_nr].fd, turn_off_message, strlen(turn_off_message) + 1);
}

void clear_screen(pollfd* poll_desc, int client_nr) {
    const char* clear_message = "\u001B[2J";
    send_data(poll_desc[client_nr].fd, clear_message, strlen(clear_message) + 1);
}

std::vector<station_print> sorted_stations(const std::map<sockaddr_in, radio_station>& radio_stations) {
    auto sorted_stations = std::vector<station_print>();
    for (auto & radio_station : radio_stations) {
        sorted_stations.push_back(std::make_pair(radio_station.second.name, radio_station.first));
    }
    std::sort(sorted_stations.begin(), sorted_stations.end());
    return sorted_stations;
}

int curr_station_nr(std::vector<station_print> sorted_stations, sockaddr_in curr_station) {
    for (int i = 0; i < (int) sorted_stations.size(); i++) {
        if (cmp_stations(sorted_stations[i].second, curr_station))
            return i;
    }
    return -1;
}

void print_interface(pollfd* poll_desc, int client_nr, std::map<sockaddr_in, radio_station>* radio_stations_ptr, pthread_mutex_t* lock_ptr, sockaddr_in* curr_station_ptr) {
    std::string interface;
    interface += "\n";
    interface += "------------------------------------------------------------------------\n\n";
    interface += " SIK Radio\n\n";
    interface += "------------------------------------------------------------------------\n\n";
    pthread_mutex_lock(lock_ptr);
    auto stations_srtd = sorted_stations(*radio_stations_ptr);
    int curr_st_nr = curr_station_nr(stations_srtd, *curr_station_ptr);
    for (int i = 0; i < (int) stations_srtd.size(); i++) {
        if (i == curr_st_nr)
            interface += " > ";
        interface += stations_srtd[i].first + "\n\n";
    }
    pthread_mutex_unlock(lock_ptr);
    interface += "------------------------------------------------------------------------\n";

    char interface_char[interface.length()+1];
    strcpy(interface_char, interface.data());
    clear_screen(poll_desc, client_nr);
    ssize_t bytes_sent = send_data(poll_desc[client_nr].fd, interface_char, interface.length()+1);
    if (bytes_sent == -1)
        fatal("Error in send\n");
}

void send_change_msg(int write_fd) {
    const char* change_msg = CHANGE_STR;
    ssize_t sent_bytes = write(write_fd, change_msg, strlen(change_msg)+1);
    if (sent_bytes == -1)
        fatal("Error in write\n");
}

bool receive_update_msg(int read_fd) {
    char msg[MSG_BUFF_SIZE-1];
    ssize_t received_bytes = read(read_fd, msg, MSG_BUFF_SIZE);
    if (received_bytes == -1)
        fatal("Error in read\n");
    return strcmp(msg, UPDATE_STR) == 0;
}

void change_station(bool up, thread_args args) {
    pthread_mutex_lock(args.lock_ptr);
    auto stations_srtd = sorted_stations(*args.radio_stations_ptr);
    int curr_st_nr = curr_station_nr(stations_srtd, *args.curr_station_ptr);
    int change_st_nr;

    if (up) {
        change_st_nr = curr_st_nr - 1;
    } else {
        change_st_nr = curr_st_nr + 1;
    }

    if (change_st_nr >= 0 && change_st_nr < (int) stations_srtd.size()) {
        *args.change_station_ptr = stations_srtd[change_st_nr].second;
        send_change_msg(args.write_fd);
    }
    pthread_mutex_unlock(args.lock_ptr);
}

void* run_ui(void* args_ptr) {
    thread_args args = *(thread_args *) args_ptr;
    free(args_ptr);
    port_t ui_port = args.ui_port;
    int write_fd = args.write_fd, read_fd = args.read_fd;
    auto radio_stations_ptr = args.radio_stations_ptr;
    pthread_mutex_t* lock_ptr = args.lock_ptr;
    sockaddr_in* curr_station_ptr = args.curr_station_ptr;
    sockaddr_in* change_station_ptr = args.change_station_ptr;

    struct sockaddr_in client_addrs[MAX_CONNS];
    struct pollfd poll_desc[MAX_CONNS];
    for (int i = 0; i < MAX_CONNS; ++i) {
        poll_desc[i].fd = -1;
        poll_desc[i].events = POLLIN;
        poll_desc[i].revents = 0;
    }

    // Creating the central socket.
    poll_desc[CENTRAL].fd = open_socket();
    bind_socket(poll_desc[CENTRAL].fd, ui_port);
    start_listening(poll_desc[CENTRAL].fd, QUEUE_LENGTH);

    poll_desc[RECV_IN].fd = read_fd;
    poll_desc[RECV_IN].events = POLLIN;

    while (true) {
        for (int i = 0; i < MAX_CONNS; ++i) {
            poll_desc[i].revents = 0;
        }

        int timeout = -1; // Wait indefinitely.
        int poll_status = poll(poll_desc, MAX_CONNS, timeout);
        if (poll_status == -1 ) {
            if (errno == EINTR)
                std::cerr << "Interrupted system call\n";
            else
                PRINT_ERRNO();
        }
        else {
            if (poll_desc[CENTRAL].revents & POLLIN) {
                // New connection.
                sockaddr_in client_addr;
                int client_fd = accept_connection(poll_desc[0].fd, &client_addr);

                int client_nr;
                bool accepted = false;
                for (int i = 2; i < MAX_CONNS; ++i) {
                    if (poll_desc[i].fd == -1) {
                        poll_desc[i].fd = client_fd;
                        poll_desc[i].events = POLLIN;
                        client_addrs[i] = client_addr;
                        client_nr = i;
                        accepted = true;
                        break;
                    }
                }
                if (!accepted) {
                    CHECK_ERRNO(close(client_fd));
                    std::cerr << "Too many clients\n";
                } else {
                    turn_off_enter(poll_desc, client_nr);
                    print_interface(poll_desc, client_nr, radio_stations_ptr, lock_ptr, curr_station_ptr);
                }
            }
            if(poll_desc[RECV_IN].revents & POLLIN) {
                if (receive_update_msg(read_fd)) {
                    for (int i = 2; i < MAX_CONNS; ++i) {
                        if (poll_desc[i].fd != -1) {
                            print_interface(poll_desc, i, radio_stations_ptr, lock_ptr, curr_station_ptr);
                        }
                    }
                }
            }
            for (int i = 2; i < MAX_CONNS; ++i) {
                if (poll_desc[i].fd != -1 && (poll_desc[i].revents & (POLLIN | POLLERR))) {
                    char key[3];
                    ssize_t received_bytes = read(poll_desc[i].fd, key, sizeof (char) * 3);
                    if (received_bytes < 0) {
                        CHECK_ERRNO(close(poll_desc[i].fd));
                        poll_desc[i].fd = -1;
                    } else if (received_bytes == 0) {
                        CHECK_ERRNO(close(poll_desc[i].fd));
                        poll_desc[i].fd = -1;
                    } else {
                        if (is_up_arrow(key, (int) received_bytes)) {
                            change_station(true, args);
                        }
                        if (is_down_arrow(key, (int) received_bytes)) {
                            change_station(false, args);
                        }

                        print_interface(poll_desc, i, radio_stations_ptr, lock_ptr, curr_station_ptr);
                    }
                }
            }
        }
    }
}