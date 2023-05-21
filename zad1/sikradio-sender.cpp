#include <cstdint>
#include <string>
#include <unistd.h>
#include "net_utils.h"
#include "utils.h"

// Polecenie do wysłania muzyki:
// sox -S "just_boring.mp3" -r 44100 -b 16 -e signed-integer -c 2 -t raw - | pv -q -L 176400 | ./sikradio-sender -a 127.0.0.1 -n "Radio Muzyczka"

// TODO:
//  - pamiętaj o zmienianiu kolejności bajtów

// Program arguments.
addr_t DEST_ADDR; // IPv4 receiver's address.
port_t DATA_PORT; // Receiver's port.
size_t PSIZE; // Package size.
std::string NAME; // Sender's name.

bool print_logs = false, debug_mode = false;

// TODO:
//   testy:
//   - najpierw po prostu w jednym senderze zrób read i write i zobacz czy ładnie słychać muzykę
//   - potem zrób przetwarzanie na i z datagramów ale nie przez sieć ?
//   -

//

/**
 * Reads data from stdin.
 */
ssize_t read_music(byte_t* buff) {
    ssize_t bytes_read = read(STDIN_FILENO, buff, PSIZE);
    std::cout << "Read " << bytes_read << " bytes.\n";
    if (bytes_read < 0)
        PRINT_ERRNO();
    return bytes_read;
}

/**
 * Reads a package of data and fills the datagram. Returns number of bytes read.
 */
ssize_t fill_audio_datagram(byte_t* datagram, uint64_t session_id, uint64_t first_byte_num) {
    ssize_t bytes_read = read_music(datagram + 16);

    session_id = htobe64(session_id);
    first_byte_num = htobe64(first_byte_num);

    memcpy(datagram, &session_id, 8);
    memcpy(datagram + 8, &first_byte_num, 8);

    return bytes_read;
}

/**
 * Sends data via UDP to DEST_ADDR on port DATA_PORT. It sends
 * the data in packages of size PSIZE.
 */
void read_and_send_music() {
    struct sockaddr_in send_address = get_address(DEST_ADDR.data(), DATA_PORT);

    int socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0)
        PRINT_ERRNO();

    byte_t datagram[PSIZE + 16];
    uint64_t session_id = time(nullptr);
    uint64_t first_byte_num = 0;
    uint8_t debug_byte = 0, debug_count = 0;
    while (true) {
        debug_count++;
        ssize_t bytes_read = fill_audio_datagram(datagram, session_id, first_byte_num);

        if (debug_mode) {
            if (bytes_read <= 0)
                break;
        } else {
            if ((size_t) bytes_read < PSIZE)
                break;
        }

        if (debug_mode) {
            // memset(datagram + 16, 1, PSIZE);
            debug_byte = 0;
            datagram[16] = debug_count;
            for (int i = 1; i < PSIZE; i++) {
                datagram[i + 16] = debug_byte;
                debug_byte++;
            }
        }
//        std::cout << "Datagram: \n";
//        print_bytes(datagram, 8);
//        print_bytes(datagram + 8, 8);
        // print_bytes(datagram + 16, PSIZE, "Bytes sent:");
//        std::cout << "\n\n";

        ssize_t bytes_sent = send_data_to(socket_fd, &send_address, datagram, PSIZE + 16);
        assert(bytes_read + 16 == bytes_sent);
        first_byte_num += bytes_read;
    }

    if (print_logs) std::cout << "Bytes sent in total: " << first_byte_num << "\n";
    CHECK_ERRNO(close(socket_fd));
}

int main(int argc, char* argv[]) {
    get_options(true, argc, argv, &DEST_ADDR, &DATA_PORT, nullptr, &PSIZE, &NAME);

    read_and_send_music();

    exit(0);
}