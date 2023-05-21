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
size_t PSIZE; // Size of audio data in last received package.

// Important: ordering from byte0! (byte0 <-> 0)
uint64_t play_byte; // Number of first byte of the package that's gonna be transmitted to stdout next.
uint64_t write_byte; // Number of the last written byte + 1.

bool print_logs = false, print_logs_2 = false, debug_mode = false;
uint64_t debug_bytes_played = 0, debug_bytes_skipped = 0, max_skip = 0;

audio_pack read_datagram(const byte_t* datagram, size_t package_size) {
    if (print_logs) {
        std::cout << "Reading datagram\n";
        // print_bytes(datagram, package_size);
    }
    audio_pack package{};
    memcpy(&package.session_id, datagram, 8);
    package.session_id = be64toh(package.session_id);
    memcpy(&package.first_byte_num, datagram + 8, 8);
    package.first_byte_num = be64toh(package.first_byte_num);
    package.audio_data = (byte_t*) malloc((package_size - 16) * sizeof(byte_t)); // TODO: free
    memcpy(package.audio_data, datagram + 16, package_size - 16);
    return package;
}

void init_connection(struct pollfd* poll_desc) {
    if (print_logs) std::cout << "Listening on port " << DATA_PORT << "\n";

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
    ssize_t package_size = receive_data_from(socket_fd, &incoming_address, start_buffer, BSIZE + 1);
    if (sender_address.sin_addr.s_addr != incoming_address.sin_addr.s_addr) // TODO: przetestować czy z innych adresów nie przychodzi
        return -1;
    return package_size;
}

void new_audio_session(audio_pack start_package, size_t new_PSIZE, byte_t* buffer) {
    playing = false;
    play_byte = start_package.first_byte_num;
    write_byte = start_package.first_byte_num;
    PSIZE = new_PSIZE;

    last_session_id = start_package.session_id;
    memset(buffer, 0, BSIZE); // Cleaning the buffer.

    debug_bytes_played = 0;
    debug_bytes_skipped = 0;
}

// TODO: here look for potential errors!! Most bug-prone part of the code I think!!
// TODO: test this
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
            uint64_t write_ptr = write_byte % eff_buffer_size; // Pointer on the next place in the buffer to write into.
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

        uint64_t tmp = play_byte;
        // Update 'pointers'.
        play_byte = (size_t) std::max((ssize_t) play_byte, (ssize_t) (new_write_byte - eff_buffer_size)); // TODO: does this make sense
        uint64_t skip = play_byte - tmp;
        if (skip > 100000) {
            if (print_logs) std::cout << "Bullshit\n";
        }
        max_skip = std::max(max_skip, skip);
        debug_bytes_skipped += skip;

        write_byte = new_write_byte;
    }

    // Copy audio_data from package to buffer.
    memcpy(buffer + (package.first_byte_num % eff_buffer_size), package.audio_data, PSIZE); // TODO
    // if (print_logs) print_bytes(buffer, BSIZE, "Buffer:");
}

void handle_new_package(size_t package_size, const byte_t* datagram, byte_t* buffer) {
    audio_pack package = read_datagram(datagram, package_size);

    if (package.session_id > last_session_id) { // New session.
        new_audio_session(package, package_size - 16, buffer);
    }
    if (package.session_id >= last_session_id) {
        write_package_to_buffer(package, buffer);
        // write(STDOUT_FILENO, datagram + 16, package_size - 16);
    }
    // If session_id < last_session_id then ignore_package;

    if (debug_mode) {
        playing = true;
    } else {
        playing |= package.first_byte_num >= play_byte + (BSIZE * 3 / 4); // TODO: make sure this makes sense.
    }
    if (print_logs) {
        std::cout << std::dec << "Handling a new package\n";
        std::cout << "first_byte_num=" << package.first_byte_num << ", play_byte=" << play_byte << "\n";
        std::cout << "play_byte+(BSIZE*3/4)=" << play_byte + (BSIZE*3/4) << "\n";
        std::cout << "playing=" << playing << "\n";
        std::cout << "PSIZE=" << PSIZE << "\n";

    }
    if (print_logs_2) {
        size_t eff_buffer_size = BSIZE - BSIZE % PSIZE;
        std::cout << std::dec << "WRITE_BYTE " << write_byte % eff_buffer_size << "         PLAY_BYTE " << play_byte % eff_buffer_size << "\n";
    }
}

/**
 * TODO
 */
void transmit_music() {
    byte_t start_buffer[BSIZE + 1];
    byte_t buffer[BSIZE];
    bool missing[BSIZE]; // TODO: change to a set?

    struct pollfd poll_desc{};
    init_connection(&poll_desc);

    // TODO: check out event pollout
    // TODO: should we only exit from receiver when there's keyboard interruption? -> handle ctrl+C
    int debug_count = 0, debug_step = 1;
    do {
        // Debugging
//        for (int i = 0; i < 10; i++)
//            std::cout << "PLAYINGPLAYINGPLAYINGPLAYINGPLAYINGPLAYINGPLAYINGPLAYINGPLAYINGPLAYINGPLAYINGPLAYINGPLAYINGPLAYING\n";
        // PART 1 : receive a new package
        debug_count++;

        poll_desc.revents = 0;
        // If nothing can be played (written to stdout), block on poll.
        int timeout = playing && play_byte < write_byte ? 0 : -1; // TODO: test if we block?
        // TODO: should we use a non-blocking version of poll?
        if (print_logs) std::cout << "Doing poll\n";
        int poll_status = poll(&poll_desc, 1, timeout);
        if (poll_status == -1) {
            if (errno == EINTR)
                std::cerr << "Interrupted system call\n";
            else
                PRINT_ERRNO();
        } else {
            if (poll_desc.revents & POLL_IN) {
                if (print_logs) std::cout << "Got POLL_IN\n";
                ssize_t package_size = receive_new_package(poll_desc.fd, start_buffer);
                if (print_logs) std::cout << "Received " << package_size << " bytes\n";
                if (package_size > 0 && (size_t) package_size - 16 <= BSIZE) {
                    handle_new_package(package_size, start_buffer, buffer);
                }
            }
        }

        // PART 2 : transmitting data to stdout (playing music)
        if (print_logs) std::cout << std::dec << "play_byte=" << play_byte << ", write_byte=" << write_byte << "\n";
        if (playing && play_byte < write_byte) {
            size_t eff_buffer_size = BSIZE - BSIZE % PSIZE;
            // ssize_t bytes_written = write(STDOUT_FILENO, buffer + (play_byte % eff_buffer_size), PSIZE);
            // assert(bytes_written > 0 && (size_t) bytes_written == PSIZE);
            // debug_bytes_played += bytes_written;
            if (print_logs) {
                // std::cout << "PLAYING\nWritten " << bytes_written << " bytes to stdout\n";
                std::cout << "(play_byte % eff_buffer_size) = " << (play_byte % eff_buffer_size) << ", PSIZE = " << PSIZE << "\n";
                std::cout << "----------------------\n";
                std::cout << "Bytes played in total: " << debug_bytes_played << "\n";
                std::cout << "Total bytes skipped: " << debug_bytes_skipped << "\n";
                std::cout << "Largest skip: " << max_skip << "\n";
                std::cout << "----------------------\n";
                print_bytes(buffer + (play_byte % eff_buffer_size), PSIZE, "Bytes written:");
            }
            // print_bytes(buffer + (play_byte % eff_buffer_size), bytes_written, "Bytes written:");
//            if (bytes_written < 0 || (size_t) bytes_written != PSIZE) {
//                assert(false);
//            }
            play_byte += PSIZE;
        }

    } while (true);

    CHECK_ERRNO(close(poll_desc.fd));
}

int main(int argc, char* argv[]) {
    get_options(false, argc, argv, &SRC_ADDR, &DATA_PORT, &BSIZE);
    transmit_music();
}