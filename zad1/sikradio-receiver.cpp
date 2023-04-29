#include <cstdint>
#include <string>
#include <poll.h>
#include "net_utils.h"
#include "utils.h"

// TODO:
//  - trzeba trzymać brakujące pakiety gdzieś (poza głównym buforem)

// Program arguments.
addr_t SRC_ADDR; // IPv4 sender's address.
port_t DATA_PORT; // Receiver's port.
size_t BSIZE; // Buffer size.

static uint64_t last_session_id = 0;
static uint64_t byte0;

/**
 * Get data from the music sender.
 */
void get_music() {
    std::cout << "Listening on port " << DATA_PORT << "\n";

    byte_t buffer[BSIZE];
    int socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0)
        PRINT_ERRNO();
    bind_socket(socket_fd, DATA_PORT);

    size_t read_length;
    do {
        read_length = receive_data_from(socket_fd, SRC_ADDR, &buffer, BSIZE); // TODO: what should max_length be equal to?
        printf("received %zd bytes from sender\n", read_length); // note: we specify the length of the printed string
        print_bytes(buffer, read_length, (char *) "Bytes:");
        std::cout << "\n";
    } while (read_length > 0);

    CHECK_ERRNO(close(socket_fd));
}

void read_datagram(const byte_t* datagram, uint64_t* session_id, uint64_t* first_byte_num, byte_t* data, size_t package_size) {
    memcpy(session_id, datagram, 8);
    memcpy(first_byte_num, datagram + 8, 8);
    memcpy(data, datagram + 16, package_size - 16);
}

void init_connection(struct pollfd* poll_desc) {
    std::cout << "Listening on port " << DATA_PORT << "\n";

    int socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0)
        PRINT_ERRNO();
    bind_socket(socket_fd, DATA_PORT);

    poll_desc->fd = socket_fd;
    poll_desc->events = POLLIN;
}

/**
 * Receives a package from any address.
 * @return Package size (maximum is BSIZE + 1) or -1 if incoming address is different than SRC_ADDR.
 */
ssize_t receive_new_package(int socket_fd, byte_t* start_buffer) {
    struct sockaddr_in sender_address = get_address(SRC_ADDR.data(), 0);
    struct sockaddr_in incoming_address;
    ssize_t package_size = receive_data_from(socket_fd, &incoming_address, &start_buffer, BSIZE + 1);
    if (sender_address.sin_addr.s_addr != incoming_address.sin_addr.s_addr) // TODO: przetestować czy z innych adresów nie przychodzi
        return -1;
    return package_size;
}

void handle_new_package(size_t package_size, const byte_t* start_buffer) {
    uint64_t session_id, first_byte_num;
    byte_t data[package_size - 16];
    read_datagram(start_buffer, &session_id, &first_byte_num, data, package_size);
    // if (session_id < last_session_id) ignore;
    if (session_id > last_session_id) { // New session.
        last_session_id = session_id;
        byte0 = first_byte_num;
    }
    if (session_id >= last_session_id) {
        // TODO new function?
    }
}


/**
 * Send data to stdout.
 */
void play_music() {
    byte_t start_buffer[BSIZE + 1];
    byte_t buffer[BSIZE];
    bool missing[BSIZE];

    struct pollfd* poll_desc;
    init_connection(poll_desc);

    do {
        poll_desc->revents = 0;
        // We're setting timeout to 0 - if data hasn't been sent to receiver we don't wait.
        int poll_status = poll(poll_desc, 1, 0);
        if (poll_status == -1) {
            if (errno == EINTR)
                std::cerr << "Interrupted system call\n";
            else
                PRINT_ERRNO();
        } else {
            if (poll_desc->revents & POLL_IN) {
                // TODO Handle new package
                ssize_t package_size = receive_new_package(poll_desc->fd, start_buffer);
                if (package_size > 0 && (size_t) package_size <= BSIZE) {

                }
            }
        }
    } while (true); // TODO
}

int main(int argc, char* argv[]) {
    get_options(false, argc, argv, &SRC_ADDR, &DATA_PORT, &BSIZE);
    get_music();
}