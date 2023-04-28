#include <cstdint>
#include <string>
#include <iostream>
#include <stdexcept>
#include <boost/program_options.hpp>
#include "net_utils.h"
#include "utils.h"

// TODO:
//  - pamiętaj o zmienianiu kolejności bajtów
//  - jak czytać dane z stdin? std::cin?

// Program arguments.
addr_t DEST_ADDR; // IPv4 receiver's address.
port_t DATA_PORT; // Receiver's port.
size_t PSIZE; // Package size.
std::string NAME; // Sender's name.

/**
 * Reads data from stdin.
 */
void get_music() {

}

/**
 * Sends data via UDP to DEST_ADDR on port DATA_PORT. It sends
 * the data in packages of size PSIZE.
 */
void send_music() {
    struct sockaddr_in send_address = get_address(DEST_ADDR.data(), DATA_PORT);

    int socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0)
        PRINT_ERRNO();

    std::string message = "Oto jest wiadomość. Dłuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuga";
    send_message_to(socket_fd, &send_address, message.c_str(), message.length());

    CHECK_ERRNO(close(socket_fd));
}

int main(int argc, char* argv[]) {
    get_options(true, argc, argv, &DEST_ADDR, &DATA_PORT, nullptr, &PSIZE, &NAME);
    send_music();
}