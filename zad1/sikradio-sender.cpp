#include <cstdint>
#include <string>
#include <unistd.h>
#include <poll.h>
#include "utils.h"

/* Descriptors related constants. */
#define STDIN 0
#define CTRL 1

#define TTL_VALUE     4

/* Program arguments. */
addr_t MCAST_ADDR; // IPv4 multicast address.
port_t DATA_PORT; // Port for audio transfer.
port_t CTRL_PORT; // Port for communication with control protocol.
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

/**
 * Initializes network connections.
 * @return Descriptor for audio socket.
 */
int init_connection(struct pollfd* poll_desc) {
    int audio_socket_fd = socket(PF_INET, SOCK_DGRAM, 0); // UDP socket.
    if (audio_socket_fd < 0)
        PRINT_ERRNO();

    // Configuring multicasting.
    int optval = 1;
    CHECK_ERRNO(setsockopt(audio_socket_fd, SOL_SOCKET, SO_BROADCAST, (void *) &optval, sizeof optval));
    optval = TTL_VALUE;
    CHECK_ERRNO(setsockopt(audio_socket_fd, IPPROTO_IP, IP_MULTICAST_TTL, (void *) &optval, sizeof optval));

    struct sockaddr_in remote_address{};
    remote_address.sin_family = PF_INET;
    remote_address.sin_port = htons(DATA_PORT);
    if (inet_aton(MCAST_ADDR.c_str(), &remote_address.sin_addr) == 0) {
        fatal("ERROR: inet_aton - invalid multicast address\n");
    }

    connect_socket(audio_socket_fd, &remote_address);

    poll_desc[STDIN].fd = STDIN_FILENO;
    poll_desc[STDIN].events = POLLIN;

    int ctrl_socket = socket(PF_INET, SOCK_DGRAM, 0); // UDP socket.
    if (ctrl_socket < 0)
        PRINT_ERRNO();
    bind_socket(ctrl_socket, CTRL_PORT);

    poll_desc[CTRL].fd = ctrl_socket;
    poll_desc[CTRL].events = POLLIN;

    return audio_socket_fd;
}

int main(int argc, char* argv[]) {
    get_options(true, argc, argv, &MCAST_ADDR, &DATA_PORT, &CTRL_PORT, nullptr, &PSIZE, &NAME);

    struct pollfd poll_desc[2];
    int audio_socket_fd = init_connection(poll_desc);

    byte_t datagram[PSIZE + 16];
    uint64_t session_id = time(nullptr);
    uint64_t first_byte_num = 0;

    int timeout = -1; // Wait indefinitely. // TODO if we change timeout we need to handle poll_status == 0
    while (true) {
        poll_desc[STDIN].revents = 0;
        poll_desc[CTRL].revents = 0;

        int poll_status = poll(poll_desc, 2, timeout);
        if (poll_status == -1) {
            if (errno == EINTR)
                std::cerr << "Interrupted system call\n";
            else
                PRINT_ERRNO();
        } else {
            if (poll_desc[STDIN].revents & POLLIN) {
                size_t bytes_read = fill_audio_datagram(datagram, session_id, first_byte_num);
                size_t total_bytes_read = bytes_read;
                while (total_bytes_read < PSIZE) {
                    if (bytes_read == 0) {
                        CHECK_ERRNO(close(audio_socket_fd));
                        exit(0);
                    }
                    bytes_read = refill_audio_datagram(datagram, total_bytes_read);
                    total_bytes_read += bytes_read;
                }

                send_data(audio_socket_fd, datagram, PSIZE + 16); // Multicast audio data.
                first_byte_num += PSIZE;
            }
            if (poll_desc[CTRL].revents & POLLIN) {
                ;
            }
        }
    }
}