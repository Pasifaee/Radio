#include <cstdint>
#include <string>
#include <poll.h>
#include "net_utils.h"
#include "utils.h"

// TODO:
//  - brakujące pakiety
//  - milsza obsługa błędóœ (staramy się naprawiać sytuację)
//  - przetestować

// Program arguments.
addr_t SRC_ADDR; // IPv4 sender's address.
port_t DATA_PORT; // Receiver's port.
size_t BSIZE; // Buffer size.

static uint64_t last_session_id = 0;
static bool playing;

// Important: ordering from byte0! (byte0 <-> 0)
uint64_t play_byte; // Number of first byte of the package that's gonna be transmitted to stdout next.
uint64_t write_byte; // Number of the last written byte + 1.

audio_pack read_datagram(const byte_t* datagram, size_t package_size) {
    audio_pack package{};
    memcpy(&package.session_id, datagram, 8);
    memcpy(&package.first_byte_num, datagram + 8, 8);
    memcpy(package.audio_data, datagram + 16, package_size - 16);
    package.PSIZE = package_size - 16;
    return package;
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
    struct sockaddr_in incoming_address{};
    ssize_t package_size = receive_data_from(socket_fd, &incoming_address, &start_buffer, BSIZE + 1);
    if (sender_address.sin_addr.s_addr != incoming_address.sin_addr.s_addr) // TODO: przetestować czy z innych adresów nie przychodzi
        return -1;
    return package_size;
}

void new_audio_session(audio_pack start_package, byte_t* buffer) {
    playing = false;
    play_byte = start_package.first_byte_num;
    write_byte = start_package.first_byte_num;

    last_session_id = start_package.session_id;
    memset(buffer, 0, BSIZE); // Cleaning the buffer.
}

// TODO: here look for be potential errors!! Most bug-prone part of the code I think!!
// TODO: test this
void write_package_to_buffer(const audio_pack package, byte_t* buffer) {
    assert(package.first_byte_num % package.PSIZE == 0);
    size_t eff_buffer_size = BSIZE - BSIZE % package.PSIZE;

    // If the incoming package is 'older' than the oldest currently in the buffer, we ignore it.
    if (package.first_byte_num < write_byte - eff_buffer_size)
        return;

    if (package.first_byte_num >= write_byte) { // Buffer's cycle boundary (write_byte) needs to move and parts of buffer need to be cleaned.
        uint64_t clean_until = package.first_byte_num;
        uint64_t new_write_byte = package.first_byte_num + package.PSIZE;
        // Cleaning the buffer. What parts of the buffer we clean depends on how much the cycle boundary (write_byte) moved.
        if (clean_until - write_byte < eff_buffer_size) {
            if (clean_until % eff_buffer_size > write_byte % eff_buffer_size) {
                // Clean a part of the buffer to the right of the write_byte.
                memset(buffer + write_byte, 0, clean_until - write_byte);
            } else {
                // Clean the buffer to the right of the write_byte and a part in the beginning.
                memset(buffer + write_byte, 0, eff_buffer_size - write_byte);
                memset(buffer, 0, clean_until % eff_buffer_size);
            }
        } else {
            // Clean the whole buffer.
            memset(buffer, 0, eff_buffer_size);
        }
        // Update 'pointers'.
        play_byte = std::max(play_byte, new_write_byte - eff_buffer_size);
        write_byte = new_write_byte;
    }

    // Copy audio_data from package to buffer.
    memcpy(buffer + (package.first_byte_num % eff_buffer_size), package.audio_data, package.PSIZE);
}

void handle_new_package(size_t package_size, const byte_t* datagram, byte_t* buffer) {
    audio_pack package = read_datagram(datagram, package_size);

    if (package.session_id > last_session_id) { // New session.
        new_audio_session(package, buffer);
    }
    if (package.session_id >= last_session_id) {
        write_package_to_buffer(package, buffer);
    }
    // If session_id < last_session_id then ignore_package;

    playing |= package.first_byte_num >= play_byte + (BSIZE*3/4); // TODO: make sure this makes sense.
}

/**
 * TODO
 */
void receive_and_play_music() {
    byte_t start_buffer[BSIZE + 1];
    byte_t buffer[BSIZE];
    bool missing[BSIZE]; // TODO: change to a set?

    struct pollfd* poll_desc;
    init_connection(poll_desc);

    // TODO: should we only exit from receiver when there's keyboard interruption? -> handle ctrl+C
    do {
        // PART 1 : receive a new package
        poll_desc->revents = 0;

        // TODO: if there's nothing to transmit to stdout, then we should block on poll
        // TODO: should we use a non-blocking version of poll?
        int poll_status = poll(poll_desc, 1, 0); // Timeout = 0 - if data hasn't been sent to receiver we don't wait.
        if (poll_status == -1) {
            if (errno == EINTR)
                std::cerr << "Interrupted system call\n";
            else
                PRINT_ERRNO();
        } else {
            if (poll_desc->revents & POLL_IN) {
                ssize_t package_size = receive_new_package(poll_desc->fd, start_buffer);
                if (package_size > 0 && (size_t) package_size <= BSIZE) {
                    handle_new_package(package_size, start_buffer, buffer);
                }
            }
        }

        // PART 2 : transmitting data to stdout (playing music)

    } while (true);

    CHECK_ERRNO(close(poll_desc->fd));
}

int main(int argc, char* argv[]) {
    get_options(false, argc, argv, &SRC_ADDR, &DATA_PORT, &BSIZE);
    // ...
}