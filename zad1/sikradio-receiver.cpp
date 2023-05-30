#include <cstdint>
#include <string>
#include <poll.h>
#include <pthread.h>
#include "net_utils.h"
#include "utils.h"
#include "ui.h"

/* Descriptors related constants. */
#define N_FDS 4
#define CTRL 0
#define AUDIO_IN 1
#define STDOUT 2
#define UI_IN 3

/* Other constants. */
#define LOOKUP_FREQ 5000 // Frequency of sending lookup messages in milliseconds.
#define STATION_LOST_T 20000 // Time after which a radio station is considered to be lost, if doesn't respond, in milliseconds.

/* Program arguments. */
addr_t DISCOVER_ADDR; // Address for looking up radio stations.
port_t DATA_PORT; // Port for audio transfer.
port_t CTRL_PORT; // Port for communication with control protocol.
port_t UI_PORT;
size_t BSIZE; // Buffer size.
std::string NAME; // Name of desired sender.

size_t PSIZE; // Size of audio data in last received package.
addr_t MCAST_ADDR; // Current station's multicast address.
int UI_OUT_FD;
uint64_t BYTE0;
uint64_t last_session_id = 0;
bool playing;
sockaddr_in curr_station;
bool connected = false, connected_name = false;
pthread_mutex_t lock;

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

    last_session_id = start_package.session_id;
    memset(buffer, 0, BSIZE); // Cleaning the buffer.
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
    sockaddr_in sender_addr;
    size_t package_size = receive_data_from(poll_desc[AUDIO_IN].fd, &sender_addr, datagram, BSIZE + 1);
    if (package_size <= 0 || (size_t) package_size - 2 * sizeof (uint64_t) > BSIZE)
        return;

    audio_pack package = read_datagram(datagram, package_size);

    if (package.session_id > last_session_id) { // New session.
        new_audio_session(package, package_size - 2 * sizeof (uint64_t), buffer);
    }
    // print_missing_packages(package);
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

void send_ui_update_msg() {
    const char* update_msg = UPDATE_STR;
    ssize_t sent_bytes = write(UI_OUT_FD, update_msg, strlen(update_msg)+1);
    if (sent_bytes == -1)
        fatal("Error in write\n");
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

/**
 * Removes radio stations that haven't sent a REPLY message in the last STATION_LOST_T ms from the radio stations map.
 * @return True if station we've been currently connected to has been removed, false otherwise.
 */
bool remove_unresponsive() {
    timeval now;
    gettimeofday(&now, nullptr);
    bool switch_station = false;

    auto end = radio_stations.end();
    for (auto it = radio_stations.begin(); it != end; ) {
        if (time_diff(now, it->second.last_reply) > STATION_LOST_T) {
            switch_station |= connected && cmp_stations(it->first, curr_station);
            pthread_mutex_lock(&lock);
            it = radio_stations.erase(it);
            send_ui_update_msg();
            pthread_mutex_unlock(&lock);
        } else {
            it++;
        }
    }

    return switch_station;
}

void connect_to_station(pollfd* poll_desc, struct ip_mreq* ip_mreq, sockaddr_in station_addr, std::string mcast_addr, port_t data_port, std::string name) {
    MCAST_ADDR = mcast_addr;
    DATA_PORT = data_port;

    int audio_socket = socket(PF_INET, SOCK_DGRAM, 0);
    if (audio_socket < 0)
        PRINT_ERRNO();

    ip_mreq->imr_interface.s_addr = htonl(INADDR_ANY);
    if (inet_aton(MCAST_ADDR.c_str(), &ip_mreq->imr_multiaddr) == 0) {
        fatal("inet_aton - invalid multicast address\n");
    }
    CHECK_ERRNO(setsockopt(audio_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *) ip_mreq, sizeof *ip_mreq));
    bind_socket(audio_socket, DATA_PORT);

    poll_desc[AUDIO_IN].fd = audio_socket;

    connected = true;
    connected_name = (name == NAME);
    pthread_mutex_lock(&lock);
    curr_station = station_addr;
    pthread_mutex_unlock(&lock);
    send_ui_update_msg();
}

void disconnect_from_station(pollfd* poll_desc, struct ip_mreq* ip_mreq) {
    CHECK_ERRNO(setsockopt(poll_desc[AUDIO_IN].fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (void *) ip_mreq, sizeof(ip_mreq)));
    CHECK_ERRNO(close(poll_desc[AUDIO_IN].fd));
    poll_desc[AUDIO_IN].fd = -1;

    connected = false;
    connected_name = false;
    send_ui_update_msg();
}

void switch_station(pollfd* poll_desc, struct ip_mreq* ip_mreq) {
    disconnect_from_station(poll_desc, ip_mreq);
    if (radio_stations.empty()) {
        return;
    }
    for (const auto& station : radio_stations) {
        if (station.second.name == NAME) {
            connect_to_station(poll_desc, ip_mreq, station.first, station.second.mcast_addr, station.second.data_port, station.second.name);
            return;
        }
    }
    auto some_station = radio_stations.begin();
    connect_to_station(poll_desc, ip_mreq, some_station->first, some_station->second.mcast_addr, some_station->second.data_port, some_station->second.name);

}

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
            pthread_mutex_lock(&lock);
            radio_stations.insert({sender_addr, radio_station{msg.name, msg.mcast_addr, msg.data_port, now}});
            send_ui_update_msg();
            pthread_mutex_unlock(&lock);
            if (!connected || (!connected_name && msg.name == NAME)) {
                if (connected)
                    disconnect_from_station(poll_desc, ip_mreq);
                connect_to_station(poll_desc, ip_mreq, sender_addr, msg.mcast_addr, msg.data_port, msg.name);
            }
        } else { // We're assuming that a station can't change its name, multicast address or data port.
            r->second.last_reply = now;
        }
    }
}

void reset_revents(pollfd* poll_desc) {
    poll_desc[AUDIO_IN].revents = 0;
    poll_desc[STDOUT].revents = 0;
    poll_desc[CTRL].revents = 0;
    poll_desc[UI_IN].revents = 0;
}

void init_connection(struct pollfd* poll_desc) {
    poll_desc[AUDIO_IN].fd = -1;
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

void transmit_music(int from_ui_fd) {
    byte_t buffer[BSIZE];

    struct pollfd poll_desc[N_FDS];
    poll_desc[UI_IN].fd = from_ui_fd;
    poll_desc[UI_IN].events = POLLIN;
    struct ip_mreq ip_mreq{};
    init_connection(poll_desc);

    timer lookup_timer = new_timer(LOOKUP_FREQ);
    while (true) {
        reset_revents(poll_desc);

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
                if (remove_unresponsive()) {
                    // Station we were connected to was just removed. We need to switch to a new station.
                    switch_station(poll_desc, &ip_mreq);
                }
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
            if (poll_desc[UI_IN].revents & POLLIN) {
                char message[100];
                ssize_t received_bytes = read(poll_desc[UI_IN].fd, message, 100);
                if (received_bytes == -1)
                    fatal("Error in read\n");
                std::cout << message << "\n";

                // DEBUG
                char* message2 = "message to ui\n";
                ssize_t b = write(UI_OUT_FD, message2, strlen(message2));
                std::cout << "Sent " << b << " bytes\n";
            }
        }

        poll_desc[STDOUT].events = connected && playing && play_byte < write_byte ? POLLOUT : 0;
    }

    // Disconnecting from multicast address.
    CHECK_ERRNO(setsockopt(poll_desc[AUDIO_IN].fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (void *) &ip_mreq, sizeof(ip_mreq)));

    CHECK_ERRNO(close(poll_desc[AUDIO_IN].fd));
}

void create_ui_thread(int write_fd, int read_fd) {
    pthread_t thread;
    auto* args = (thread_args*) malloc(sizeof(thread_args));
    *args = thread_args{UI_PORT, write_fd, read_fd, &radio_stations, &lock, &curr_station};
    CHECK_ERRNO(pthread_create(&thread, nullptr, run_ui, args));
    CHECK_ERRNO(pthread_detach(thread));
}

// TODO: check if command line parameters are correct
int main(int argc, char* argv[]) {
    get_options(false, argc, argv, &DISCOVER_ADDR, &NAME, &CTRL_PORT, &UI_PORT, &BSIZE);

    int from_ui[2];
    CHECK_ERRNO(pipe(from_ui));
    int to_ui[2];
    CHECK_ERRNO(pipe(to_ui));
    UI_OUT_FD = to_ui[1];
    create_ui_thread(from_ui[1], to_ui[0]);
    transmit_music(from_ui[0]);
}