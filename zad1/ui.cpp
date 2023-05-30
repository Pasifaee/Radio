#include <poll.h>
#include "ui.h"

#define CENTRAL 0
#define RECV_IN 1

std::string print_interface(std::map<sockaddr_in, radio_station>* radio_stations_ptr, pthread_mutex_t* lock_ptr) {
    std::string interface;
    interface += "------------------------------------------------------------------------\n\n";
    interface += " SIK Radio\n\n";
    interface += "------------------------------------------------------------------------\n\n";
    pthread_mutex_lock(lock_ptr);
    for (auto it = radio_stations_ptr->begin(); it != radio_stations_ptr->end(); it++) {
        interface += "Radio \"" + it->second.name + "\"\n\n";
    }
    pthread_mutex_unlock(lock_ptr);
    interface += "------------------------------------------------------------------------\n";
    return interface;
}

void* run_ui(void* args_ptr) {
    thread_args args = *(thread_args *) args_ptr;
    free(args_ptr);
    port_t ui_port = args.ui_port;
    int write_fd = args.write_fd, read_fd = args.read_fd;
    auto radio_stations_ptr = args.radio_stations_ptr;
    auto lock_ptr = args.lock_ptr;

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

                bool accepted = false;
                for (int i = 2; i < MAX_CONNS; ++i) {
                    if (poll_desc[i].fd == -1) {
                        poll_desc[i].fd = client_fd;
                        poll_desc[i].events = POLLIN;
                        client_addrs[i] = client_addr;
                        accepted = true;
                        break;
                    }
                }
                if (!accepted) {
                    CHECK_ERRNO(close(client_fd));
                    std::cerr << "Too many clients\n";
                }
            }
            if(poll_desc[RECV_IN].revents & POLLIN) {
                char message[100];
                ssize_t r = read(read_fd, message, 100);
                std::cout << "[UI] Read " << r << " bytes from receiver\n";
                for (int i = 2; i < MAX_CONNS; ++i) {
                    if (poll_desc[i].fd != -1) {
                        std::string interface = print_interface(radio_stations_ptr, lock_ptr);
                        char interface_char[interface.length()+1];
                        strcpy(interface_char, interface.data());
                        ssize_t bytes_sent = send_data(poll_desc[i].fd, interface_char, interface.length()+1);
                        if (bytes_sent == -1)
                            fatal("Error in send\n");
                    }
                }
            }
            for (int i = 2; i < MAX_CONNS; ++i) {
                if (poll_desc[i].fd != -1 && (poll_desc[i].revents & (POLLIN | POLLERR))) {
                    char key[5]; // TODO: what size?
                    ssize_t received_bytes = read(poll_desc[i].fd, key, sizeof (char) * 5);
                    if (received_bytes < 0) {
                        CHECK_ERRNO(close(poll_desc[i].fd));
                        poll_desc[i].fd = -1;
                    } else if (received_bytes == 0) {
                        CHECK_ERRNO(close(poll_desc[i].fd));
                        poll_desc[i].fd = -1;
                    } else {
                        if (is_up_arrow(key, received_bytes)) {
                            char* message = "[thread] Pressed UP\n";
                            ssize_t sent_bytes = write(write_fd, message, strlen(message)+1);
                            if (sent_bytes == -1)
                                fatal("Error in write\n");
                        }
                        if (is_down_arrow(key, received_bytes))
                           ; // std::cout << "Pressed DOWN\n";
                    }
                }
            }
        }
    }
}