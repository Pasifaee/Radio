#include <poll.h>
#include "ui.h"

void* run_ui(void* ui_port_ptr) {
    int ui_port = *(port_t *) ui_port_ptr;
    free(ui_port_ptr);

    struct pollfd poll_descriptors[MAX_CONNS];
    for (int i = 0; i < MAX_CONNS; ++i) {
        poll_descriptors[i].fd = -1;
        poll_descriptors[i].events = POLLIN;
        poll_descriptors[i].revents = 0;
    }

    // Creating the central socket.
    poll_descriptors[0].fd = open_socket();
    bind_socket(poll_descriptors[0].fd, ui_port);
    start_listening(poll_descriptors[0].fd, QUEUE_LENGTH);
    std::cout << "Listening on ui port " << ui_port << "\n";

    while (true) {
        for (int i = 0; i < MAX_CONNS; ++i) {
            poll_descriptors[i].revents = 0;
        }

        int timeout = -1; // Wait indefinitely.
        int poll_status = poll(poll_descriptors, MAX_CONNS, timeout);
        if (poll_status == -1 ) {
            if (errno == EINTR)
                fprintf(stderr, "Interrupted system call\n");
            else
                PRINT_ERRNO();
        }
        else {
            if (poll_descriptors[0].revents & POLLIN) {
                // New connection.
                int client_fd = accept_connection(poll_descriptors[0].fd, nullptr);

                bool accepted = false;
                for (int i = 1; i < MAX_CONNS; ++i) {
                    if (poll_descriptors[i].fd == -1) {
                        std::cerr <<"Received new connection " << i << "\n";

                        poll_descriptors[i].fd = client_fd;
                        poll_descriptors[i].events = POLLIN;
                        accepted = true;
                        break;
                    }
                }
                if (!accepted) {
                    CHECK_ERRNO(close(client_fd));
                    std::cerr << "Too many clients\n";
                }
            }
            for (int i = 1; i < MAX_CONNS; ++i) {
                if (poll_descriptors[i].fd != -1 && (poll_descriptors[i].revents & (POLLIN | POLLERR))) {
                    char key[5];
                    ssize_t received_bytes = read(poll_descriptors[i].fd, key, sizeof (char) * 10);
                    if (received_bytes < 0) {
                        CHECK_ERRNO(close(poll_descriptors[i].fd));
                        poll_descriptors[i].fd = -1;
                    } else if (received_bytes == 0) {
                        std::cerr << "Ending connection " << i << "\n";
                        CHECK_ERRNO(close(poll_descriptors[i].fd));
                        poll_descriptors[i].fd = -1;
                    } else {
                        if (is_up_arrow(key, received_bytes))
                            std::cout << "Pressed UP\n";
                        if (is_down_arrow(key, received_bytes))
                            std::cout << "Pressed DOWN\n";
                    }
                }
            }
        }
    }
}