#include <cstdint>
#include <string>
#include <unistd.h>
#include "utils.h"

#define TTL_VALUE     4

/* Program arguments. */
addr_t MCAST_ADDR; // IPv4 multicast address.
port_t DATA_PORT; // Receiver's port.
size_t PSIZE; // Package size.
std::string NAME; // Sender's name.


/**
 * Reads data from stdin.
 */
size_t read_music(byte_t* buff, size_t n) {
    ssize_t bytes_read = read(STDIN_FILENO, buff, n);
    if (bytes_read < 0)
        PRINT_ERRNO();
    return (size_t) bytes_read;
}

/**
 * Reads a package of data and fills the datagram. Returns number of bytes read.
 */
size_t fill_audio_datagram(byte_t* datagram, uint64_t session_id, uint64_t first_byte_num) {
    memset(datagram, 0, PSIZE + 16);

    size_t bytes_read = read_music(datagram + 16, PSIZE);

    session_id = htobe64(session_id);
    first_byte_num = htobe64(first_byte_num);

    memcpy(datagram, &session_id, 8);
    memcpy(datagram + 8, &first_byte_num, 8);

    return bytes_read;
}

size_t refill_audio_datagram(byte_t* datagram, size_t from) {
    return read_music(datagram + from + 16, PSIZE - from);
}

int init_connection() {
    int socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0)
        PRINT_ERRNO();

    // Configuring multicasting.
    int optval = 1;
    CHECK_ERRNO(setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, (void *) &optval, sizeof optval));
    optval = TTL_VALUE;
    CHECK_ERRNO(setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_TTL, (void *) &optval, sizeof optval));

    struct sockaddr_in remote_address{};
    remote_address.sin_family = PF_INET;
    remote_address.sin_port = htons(DATA_PORT);
    if (inet_aton(MCAST_ADDR.c_str(), &remote_address.sin_addr) == 0) {
        fatal("ERROR: inet_aton - invalid multicast address\n");
    }

    connect_socket(socket_fd, &remote_address);
    return socket_fd;
}

/**
 * Sends data via UDP to MCAST_ADDR on port DATA_PORT. It sends
 * the data in packages of size PSIZE.
 */
void read_and_send_music() {
    int audio_socket = init_connection();

    byte_t datagram[PSIZE + 16];
    uint64_t session_id = time(nullptr);
    uint64_t first_byte_num = 0;
    while (true) {
        size_t bytes_read = fill_audio_datagram(datagram, session_id, first_byte_num);
        size_t total_bytes_read = bytes_read;
        while (total_bytes_read < PSIZE) {
            if (bytes_read == 0) {
                CHECK_ERRNO(close(audio_socket)); // TODO: Fix: getting error "bad file descriptor"
                exit(0);
            }
            bytes_read = refill_audio_datagram(datagram, total_bytes_read);
            total_bytes_read += bytes_read;
        }

        send_data(audio_socket, datagram, PSIZE + 16); // Multicast audio data.
        first_byte_num += PSIZE;
    }
}

int main(int argc, char* argv[]) {
    get_options(true, argc, argv, &MCAST_ADDR, &DATA_PORT, nullptr, &PSIZE, &NAME);

    read_and_send_music();

    exit(0);
}