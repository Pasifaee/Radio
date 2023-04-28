#include <cstdint>
#include <string>
#include <iostream>
#include <stdexcept>
#include <boost/program_options.hpp>
#include "net_utils.h"
#include "utils.h"

// Program arguments.
addr_t SRC_ADDR; // IPv4 sender's address.
port_t DATA_PORT; // Receiver's port.
size_t BSIZE; // Buffer size.

/**
 * Get data from the music sender.
 */
void get_music() {
    printf("Listening on port %u\n", DATA_PORT);

    std::byte buffer[BSIZE];
    int socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0)
        PRINT_ERRNO();
    bind_socket(socket_fd, DATA_PORT);

    size_t read_length;
    do {
        read_length = receive_message_from(socket_fd, SRC_ADDR, &buffer, sizeof(buffer)); // TODO: what should max_length be equal to?
        printf("received %zd bytes from sender: '%.*s'\n", read_length, (int) read_length, (char *) buffer); // note: we specify the length of the printed string
    } while (read_length > 0);

    CHECK_ERRNO(close(socket_fd));
}

/**
 * Send data to stdout.
 */
void play_music() {

}

int main(int argc, char* argv[]) {
    get_options(false, argc, argv, &SRC_ADDR, &DATA_PORT, &BSIZE);
    get_music();
}