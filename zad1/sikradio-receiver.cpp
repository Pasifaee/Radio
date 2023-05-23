#include <cstdint>
#include <string>
#include <poll.h>
#include "net_utils.h"
#include "utils.h"

#define IN 0
#define OUT 1

/* Program arguments. */
addr_t SRC_ADDR; // IPv4 sender's address.
port_t DATA_PORT; // Receiver's port.
size_t BSIZE; // Buffer size.

size_t PSIZE; // Size of audio data in last received package.
uint64_t BYTE0;
uint64_t last_session_id = 0;
bool playing;
std::set<uint64_t> missing;

// Important: ordering from byte0! (byte0 <-> 0)
uint64_t play_byte; // Number of first byte of the package that's gonna be transmitted to stdout next.
uint64_t write_byte; // Number of the last written byte + 1.


audio_pack read_datagram(const byte_t* datagram, size_t package_size) {
    audio_pack package{};
    memcpy(&package.session_id, datagram, 8);
    package.session_id = be64toh(package.session_id);
    memcpy(&package.first_byte_num, datagram + 8, 8);
    package.first_byte_num = be64toh(package.first_byte_num);
    package.audio_data = (byte_t*) malloc((package_size - 16) * sizeof(byte_t)); // TODO: free OR how to do this without malloc?
    memcpy(package.audio_data, datagram + 16, package_size - 16);
    return package;
}

void init_connection(struct pollfd* poll_desc) {
    int socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0)
        PRINT_ERRNO();
    bind_socket(socket_fd, DATA_PORT);

    poll_desc[IN].fd = socket_fd;
    poll_desc[IN].events = POLLIN;

    poll_desc[OUT].fd = STDOUT_FILENO;
    poll_desc[OUT].events = 0;
}

/**
 * Receives a package from any address.
 * @return Package size (maximum is BSIZE + 1) or -1 if incoming address is different than SRC_ADDR.
 */
ssize_t receive_new_package(int socket_fd, byte_t* start_buffer) {
    struct sockaddr_in sender_address = get_address(SRC_ADDR.data(), 0);
    struct sockaddr_in incoming_address{};

    ssize_t package_size = receive_data_from(socket_fd, &incoming_address, start_buffer, BSIZE + 1);
    if (sender_address.sin_addr.s_addr != incoming_address.sin_addr.s_addr) // TODO: przetestować czy z innych adresów nie przychodzi
        return -1;
    return package_size;
}

void new_audio_session(audio_pack start_package, size_t new_PSIZE, byte_t* buffer) {
    playing = false;
    BYTE0 = start_package.first_byte_num;
    play_byte = BYTE0;
    write_byte = BYTE0;
    PSIZE = new_PSIZE;
    missing.clear();

    last_session_id = start_package.session_id;
    memset(buffer, 0, BSIZE); // Cleaning the buffer.
}

// TODO: test this function
void print_missing_packages(const audio_pack package) {
    size_t eff_buffer_size = BSIZE - BSIZE % PSIZE;

    if (package.first_byte_num > write_byte) { // Missing packages between old and new write_byte.
        for (uint64_t i = write_byte; i < package.first_byte_num; i += PSIZE) {
            if (i >= BYTE0)
                missing.insert((i - BYTE0) / PSIZE);
        }
    } else if (package.first_byte_num < write_byte) { // Received package that was previously missing.
        missing.erase((package.first_byte_num - BYTE0) / PSIZE);
    }

    if (write_byte >= eff_buffer_size + BYTE0) { // Remove old packages from missing.
        auto it = missing.lower_bound((write_byte - eff_buffer_size - BYTE0) / PSIZE);
        missing.erase(missing.begin(), it);
    }

    for (auto package_nr : missing) {
        std::cerr << "MISSING " << package_nr << " BEFORE " << (package.first_byte_num - BYTE0) / PSIZE << "\n";
    }
}

void write_package_to_buffer(const audio_pack package, byte_t* buffer) {
    size_t eff_buffer_size = BSIZE - BSIZE % PSIZE;

    // If the incoming package is 'older' than the oldest currently in the buffer, we ignore it.
    if ((ssize_t) package.first_byte_num < (ssize_t) (write_byte - eff_buffer_size))
        return;

    if (package.first_byte_num >= write_byte) { // Buffer's cycle boundary (write_byte) needs to move and parts of buffer need to be cleaned.
        uint64_t clean_until = package.first_byte_num;
        uint64_t new_write_byte = package.first_byte_num + PSIZE;
        // Cleaning the buffer. Which parts of the buffer we clean depends on how much the cycle boundary (write_byte) moved.
        if (clean_until - write_byte < eff_buffer_size) {
            uint64_t write_ptr = write_byte % eff_buffer_size; // Pointer to the next place in the buffer to write into.
            if (clean_until % eff_buffer_size >= write_ptr) {
                // Clean a part of the buffer to the right of the write_byte.
                memset(buffer + write_ptr, 0, clean_until - write_byte);
            } else {
                // Clean the buffer to the right of the write_byte and a part in the beginning.
                memset(buffer + write_ptr, 0, eff_buffer_size - write_ptr);
                memset(buffer, 0, clean_until % eff_buffer_size);
            }
        } else {
            // Clean the whole buffer.
            memset(buffer, 0, eff_buffer_size);
        }

        // Update 'pointers'.
        play_byte = (size_t) std::max((ssize_t) play_byte, (ssize_t) (new_write_byte - eff_buffer_size));
        write_byte = new_write_byte;
    }

    // Copy audio_data from package to buffer.
    memcpy(buffer + (package.first_byte_num % eff_buffer_size), package.audio_data, PSIZE);
}

void handle_new_package(size_t package_size, const byte_t* datagram, byte_t* buffer) {
    audio_pack package = read_datagram(datagram, package_size);

    if (package.session_id > last_session_id) { // New session.
        new_audio_session(package, package_size - 16, buffer);
    }
    print_missing_packages(package);
    if (package.session_id >= last_session_id) {
        write_package_to_buffer(package, buffer);
    }
    // If session_id < last_session_id then ignore_package;

    playing |= package.first_byte_num >= BYTE0 + (BSIZE * 3 / 4);
}

/**
 * TODO
 *   - handle missing packages - change array to a set?
 */
void transmit_music() {
    byte_t start_buffer[BSIZE + 1];
    byte_t buffer[BSIZE];
    bool missing[BSIZE]; // TODO: change to a set?

    struct pollfd poll_desc[2];
    init_connection(poll_desc);

    int timeout = -1; // Wait indefinitely.
    do {
        poll_desc[IN].revents = 0;
        poll_desc[OUT].revents = 0;

        int poll_status = poll(poll_desc, 2, timeout);
        if (poll_status == -1) {
            if (errno == EINTR)
                std::cerr << "Interrupted system call\n";
            else
                PRINT_ERRNO();
        } else {
            if (poll_desc[IN].revents & POLLIN) {
                ssize_t package_size = receive_new_package(poll_desc[IN].fd, start_buffer);
                if (package_size > 0 && (size_t) package_size - 2 * sizeof (uint64_t) <= BSIZE) {
                    handle_new_package(package_size, start_buffer, buffer);
                }
            }
            if (poll_desc[OUT].revents & POLLOUT) {
                size_t eff_buffer_size = BSIZE - BSIZE % PSIZE;
                ssize_t bytes_written = write(STDOUT_FILENO, buffer + (play_byte % eff_buffer_size), PSIZE);
                assert(bytes_written > 0 && (size_t) bytes_written == PSIZE);

                play_byte += PSIZE;
            }
        }

        poll_desc[OUT].events = playing && play_byte < write_byte ? POLLOUT : 0;
    } while (true);

    CHECK_ERRNO(close(poll_desc[IN].fd));
}

int main(int argc, char* argv[]) {
    get_options(false, argc, argv, &SRC_ADDR, &DATA_PORT, &BSIZE);
    transmit_music();
}