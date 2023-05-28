#include <cstdint>
#include <string>
#include <poll.h>
#include "net_utils.h"
#include "utils.h"

/* Descriptors related constants. */
#define N_FDS 3
#define AUDIO_IN 0
#define STDOUT 1
#define CTRL 2

/* Other constants. */
#define LOOKUP_FREQ 5000 // Frequency of sending lookup messages in miliseconds.
#define STATION_LOST_T 10000 // Time after which a radio station is considered to be lost, if doesn't respond, in miliseconds.

/* Program arguments. */
addr_t DISCOVER_ADDR; // Address for looking up radio stations.
port_t DATA_PORT; // Port for audio transfer.
port_t CTRL_PORT; // Port for communication with control protocol.
size_t BSIZE; // Buffer size.

size_t PSIZE; // Size of audio data in last received package.
addr_t MCAST_ADDR = "224.0.0.1"; // IPv4 sender's address. // TODO: remove hardcoded value after debugging
uint64_t BYTE0;
uint64_t last_session_id = 0;
bool playing;
std::set<uint64_t> missing;

std::map<sockaddr_in, radio_station> radio_stations;

// Important: ordering from byte0! (byte0 <-> 0)
uint64_t play_byte; // Number of first byte of the package that's gonna be transmitted to stdout next.
uint64_t write_byte; // Number of the last written byte + 1.


audio_pack read_datagram(const byte_t* datagram, size_t package_size) {
    audio_pack package{};
    memcpy(&package.session_id, datagram, sizeof (uint64_t));
    package.session_id = be64toh(package.session_id);
    memcpy(&package.first_byte_num, datagram + sizeof (uint64_t), sizeof (uint64_t));
    package.first_byte_num = be64toh(package.first_byte_num);
    package.audio_data = (byte_t*) malloc((package_size - 2 * sizeof (uint64_t)) * sizeof (byte_t)); // TODO: free OR how to do this without malloc?
    memcpy(package.audio_data, datagram + 2 * sizeof (uint64_t), package_size - 2 * sizeof (uint64_t));
    return package;
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

void handle_new_package(pollfd* poll_desc, byte_t* buffer) {
    byte_t datagram[BSIZE + 1];
    size_t package_size = receive_data(poll_desc[AUDIO_IN].fd, datagram, BSIZE + 1);
    if (package_size <= 0 || (size_t) package_size - 2 * sizeof (uint64_t) > BSIZE) {
        return;
    }

    audio_pack package = read_datagram(datagram, package_size);

    if (package.session_id > last_session_id) { // New session.
        new_audio_session(package, package_size - 2 * sizeof (uint64_t), buffer);
    }
    print_missing_packages(package);
    if (package.session_id >= last_session_id) {
        write_package_to_buffer(package, buffer);
    }
    // If session_id < last_session_id then ignore_package;

    playing |= package.first_byte_num >= BYTE0 + (BSIZE * 3 / 4);
}

void play(const byte_t* buffer) {
    size_t eff_buffer_size = BSIZE - BSIZE % PSIZE;
    ssize_t bytes_written = write(STDOUT_FILENO, buffer + (play_byte % eff_buffer_size), PSIZE);
    assert(bytes_written > 0 && (size_t) bytes_written == PSIZE);

    play_byte += PSIZE;
}

void send_lookup_msg(pollfd* poll_desc, timer* lookup_timer) {
    // Send LOOKUP message.
    sockaddr_in discover_addr = get_udp_address(DISCOVER_ADDR.data(), CTRL_PORT);
    message lookup_msg{};
    lookup_msg.msg_type = LOOKUP;
    std::string lookup_msg_str = get_message_str(lookup_msg);
    send_data_to(poll_desc[CTRL].fd, &discover_addr, lookup_msg_str.c_str(), lookup_msg_str.length());

    // Wait before sending next lookup message.
    reset_timer(lookup_timer);
    poll_desc[CTRL].events = POLLIN;
}

void connect_to_station(int audio_socket, struct ip_mreq* ip_mreq, std::string mcast_addr, port_t data_port);

void handle_message(pollfd* poll_desc, struct ip_mreq* ip_mreq) {
    // Listen for REPLY messages.
    char msg_str[MSG_BUFF_SIZE];

    struct sockaddr_in sender_addr{};
    size_t msg_len = receive_data_from(poll_desc[CTRL].fd, &sender_addr, msg_str, MSG_BUFF_SIZE - 1);
    msg_str[msg_len] = 0;
    message msg = parse_message(std::string(msg_str));

    if (msg.msg_type == REPLY) {
        timeval now;
        gettimeofday(&now, nullptr);

        auto r = radio_stations.find(sender_addr);
        if (r == radio_stations.end()) {
            radio_stations.insert({sender_addr, radio_station{msg.name, msg.mcast_addr, msg.data_port, now}});
            connect_to_station(poll_desc[AUDIO_IN].fd, ip_mreq, msg.mcast_addr, msg.data_port); // TODO: this is debug version
        } else {
            r->second.last_reply = now;
        }
    }
}

void update_map() {
    timeval now;
    gettimeofday(&now, nullptr);
    auto end = radio_stations.end();
    for (auto it = radio_stations.begin(); it != end; ) {
        if (time_diff(now, it->second.last_reply) > STATION_LOST_T) {
            it = radio_stations.erase(it);
        } else {
            it++;
        }
    }
}

void connect_to_station(int audio_socket, struct ip_mreq* ip_mreq, std::string mcast_addr, port_t data_port) {
    MCAST_ADDR = mcast_addr;
    DATA_PORT = data_port;

    ip_mreq->imr_interface.s_addr = htonl(INADDR_ANY);
    if (inet_aton(MCAST_ADDR.c_str(), &ip_mreq->imr_multiaddr) == 0) {
        fatal("inet_aton - invalid multicast address\n");
    }
    CHECK_ERRNO(setsockopt(audio_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *) ip_mreq, sizeof *ip_mreq));
    bind_socket(audio_socket, DATA_PORT);
}

void disconnect_from_station(int audio_socket, struct ip_mreq* ip_mreq) {
    CHECK_ERRNO(setsockopt(audio_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (void *) &ip_mreq, sizeof(ip_mreq)));
}

void switch_to_station(int audio_socket, struct ip_mreq* ip_mreq, std::string mcast_addr, port_t data_port) {
    disconnect_from_station(audio_socket, ip_mreq);
    connect_to_station(audio_socket, ip_mreq, mcast_addr, data_port);
}

void init_connection(struct pollfd* poll_desc, struct ip_mreq* ip_mreq) {
    int audio_socket = socket(PF_INET, SOCK_DGRAM, 0);
    if (audio_socket < 0)
        PRINT_ERRNO();

    poll_desc[AUDIO_IN].fd = audio_socket;
    poll_desc[AUDIO_IN].events = POLLIN;

    poll_desc[STDOUT].fd = STDOUT_FILENO;
    poll_desc[STDOUT].events = 0;

    int ctrl_socket = socket(PF_INET, SOCK_DGRAM, 0); // UDP socket.
    if (ctrl_socket < 0)
        PRINT_ERRNO();

    int optval = 1;
    CHECK_ERRNO(setsockopt(ctrl_socket, SOL_SOCKET, SO_BROADCAST, (void *) &optval, sizeof optval));

    poll_desc[CTRL].fd = ctrl_socket;
    poll_desc[CTRL].events = POLLIN | POLLOUT;
}

void transmit_music() {
    byte_t buffer[BSIZE];

    struct pollfd poll_desc[N_FDS];
    struct ip_mreq ip_mreq{};
    init_connection(poll_desc, &ip_mreq);

    timer lookup_timer = new_timer(LOOKUP_FREQ);
    while (true) {
        poll_desc[AUDIO_IN].revents = 0;
        poll_desc[STDOUT].revents = 0;
        poll_desc[CTRL].revents = 0;

        int poll_status = poll(poll_desc, N_FDS, (int) check_time(&lookup_timer));
        if (check_time(&lookup_timer) <= 0) { // Time to send a lookup message.
            poll_desc[CTRL].events = POLLIN | POLLOUT;
        }
        if (poll_status == -1) {
            if (errno == EINTR)
                std::cerr << "Interrupted system call\n";
            else
                PRINT_ERRNO();
        } else {
            if (poll_desc[CTRL].revents & POLLOUT) {
                send_lookup_msg(poll_desc, &lookup_timer);
                update_map();
            }
            if (poll_desc[CTRL].revents & POLLIN) {
                handle_message(poll_desc, &ip_mreq);
            }
            if (poll_desc[AUDIO_IN].revents & POLLIN) {
                handle_new_package(poll_desc, buffer);
            }
            if (poll_desc[STDOUT].revents & POLLOUT) {
                play(buffer);
            }
        }

        poll_desc[STDOUT].events = playing && play_byte < write_byte ? POLLOUT : 0;
    }

    // Disconnecting from multicast address.
    CHECK_ERRNO(setsockopt(poll_desc[AUDIO_IN].fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (void *) &ip_mreq, sizeof(ip_mreq)));

    CHECK_ERRNO(close(poll_desc[AUDIO_IN].fd));
}

// TODO: check if command line parameters are correct
int main(int argc, char* argv[]) {
    get_options(false, argc, argv, &DISCOVER_ADDR, &DATA_PORT, &CTRL_PORT, &BSIZE);
    transmit_music();
}