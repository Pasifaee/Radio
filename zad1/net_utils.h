#ifndef MIMUW_SIK_TCP_SOCKETS_COMMON_H
#define MIMUW_SIK_TCP_SOCKETS_COMMON_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <signal.h>
#include <iostream>

#include "err.h"

// Types.
#define addr_t std::string
#define port_t uint16_t
#define byte_t uint8_t

// Constants.
#define NO_FLAGS 0

inline static bool valid_port(const char *string) {
    errno = 0;
    unsigned long port = strtoul(string, NULL, 10);
    if (errno != 0)
        return false;
    if (port > UINT16_MAX)
        return false;

    return true;
}

inline static uint16_t read_port(const char *string) {
    errno = 0;
    unsigned long port = strtoul(string, NULL, 10);
    PRINT_ERRNO();
    if (port > UINT16_MAX) {
        fatal("%ul is not a valid port number", port);
    }

    return (uint16_t) port;
}

inline static uint16_t get_port(struct sockaddr_in *address) {
    return ntohs(address->sin_port);
}

/// No need to free the returned string,
/// it is a pointer to a static buffer
inline static char *get_ip(struct sockaddr_in *address) {
    return inet_ntoa(address->sin_addr);
}

inline static uint16_t get_port_from_socket(int socket_fd) {
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    CHECK_ERRNO(getsockname(socket_fd, (struct sockaddr *) &address, &address_length));
    return get_port(&address);
}

inline static char *get_ip_from_socket(int socket_fd) {
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    CHECK_ERRNO(getsockname(socket_fd, (struct sockaddr *) &address, &address_length));
    return get_ip(&address);
}

inline static struct sockaddr_in get_udp_address(char *host, uint16_t port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *address_result;
    CHECK(getaddrinfo(host, NULL, &hints, &address_result));

    struct sockaddr_in address;
    address.sin_family = AF_INET; // IPv4
    address.sin_addr.s_addr =
            ((struct sockaddr_in *) (address_result->ai_addr))->sin_addr.s_addr; // IP address
    address.sin_port = htons(port);

    freeaddrinfo(address_result);

    return address;
}

inline static int open_socket() {
    int socket_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd < 0) {
        PRINT_ERRNO();
    }

    return socket_fd;
}

inline static void bind_socket(int socket_fd, uint16_t port) {
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(port);

    // bind the socket to a concrete address
    CHECK_ERRNO(bind(socket_fd, (struct sockaddr *) &server_address,
                     (socklen_t) sizeof(server_address)));
}

/// Returns the assigned port number
inline static uint16_t bind_socket_to_any_port(int socket_fd) {
    bind_socket(socket_fd, 0);
    return get_port_from_socket(socket_fd);
}

inline static void start_listening(int socket_fd, size_t queue_length) {
    CHECK_ERRNO(listen(socket_fd, queue_length));
}

inline static int accept_connection(int socket_fd, struct sockaddr_in *client_address) {
    socklen_t client_address_length = (socklen_t) sizeof(*client_address);

    int client_fd = accept(socket_fd, (struct sockaddr *) client_address, &client_address_length);
    if (client_fd < 0) {
        PRINT_ERRNO();
    }

    return client_fd;
}

inline static void connect_socket(int socket_fd, const struct sockaddr_in *address) {
    CHECK_ERRNO(connect(socket_fd, (struct sockaddr *) address, sizeof(*address)));
}

inline static ssize_t send_data(int socket_fd, const void *data, size_t length) {
    errno = 0;
    ssize_t sent_length = send(socket_fd, data, length, NO_FLAGS);
    if (sent_length < 0) {
        PRINT_ERRNO();
    }
    ENSURE(sent_length == (ssize_t) length);
    return sent_length;
}

inline static ssize_t send_data_to(int socket_fd, const struct sockaddr_in *send_address, const void *data, size_t length) {
    auto address_length = (socklen_t) sizeof(*send_address);
    errno = 0;
    ssize_t sent_length = sendto(socket_fd, data, length, NO_FLAGS,
                                 (struct sockaddr *) send_address, address_length);
    if (sent_length < 0) {
        PRINT_ERRNO();
    }
    ENSURE(sent_length == (ssize_t) length);

    return sent_length;
}

inline static bool addr_cmp(sockaddr_in addr1, sockaddr_in addr2) {
    return addr1.sin_port == addr2.sin_port && addr1.sin_addr.s_addr == addr2.sin_addr.s_addr;
}

inline static size_t receive_data(int socket_fd, void *buffer, size_t max_length) {
    errno = 0;
    ssize_t received_length = recv(socket_fd, buffer, max_length, NO_FLAGS);
    if (received_length < 0) {
        PRINT_ERRNO();
    }
    return (size_t) received_length;
}

inline static size_t receive_data_from(int socket_fd, struct sockaddr_in *client_address, void *buffer, size_t max_length) {
    auto address_length = (socklen_t) sizeof(client_address);

    ssize_t received_length;
    errno = 0;
    received_length = recvfrom(socket_fd, buffer, max_length, NO_FLAGS,
                               (struct sockaddr *) client_address, &address_length);
    if (received_length < 0)
        PRINT_ERRNO();

    return (size_t) received_length;
}

inline static void install_signal_handler(int signal, void (*handler)(int)) {
    struct sigaction action;
    sigset_t block_mask;

    sigemptyset(&block_mask);
    action.sa_handler = handler;
    action.sa_mask = block_mask;
    action.sa_flags = 0;

    CHECK_ERRNO(sigaction(signal, &action, NULL));
}

inline static void set_port_reuse(int socket_fd) {
    int option_value = 1;
    CHECK_ERRNO(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &option_value, sizeof(option_value)));
}

#endif //MIMUW_SIK_TCP_SOCKETS_COMMON_H
