// Copyright [2016] [Pedro Vicente]
// Fixes     [2025] [Oleksiy Zakharov]
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "socket.hpp" // IWYU pragma: keep

#include <common/runners.h> // IWYU pragma: keep
#include <memory.h>         // NOLINT
#include <poll.h>
#include <string.h> // NOLINT
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <iterator>
#include <list>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER)
    #include <winsock2.h>
    #include <ws2tcpip.h>

struct WSAInitializer
{
    WSAInitializer()
    {
        static_assert(false, "Please revise if this OK for Windows");
        if (WSAStartup(MAKEWORD(2, 0), &ws_data) != 0)
        {
            throw std::runtime_error("Windows' startup failure.");
        }
    }
    ~WSAInitializer()
    {
        WSACleanup();
    }
};

using addr_len_t = int;
#else
    #include <arpa/inet.h>
    #include <netdb.h> //hostent
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <syslog.h>

    #include <asm-generic/socket.h>

struct WSAInitializer
{
};
using addr_len_t = socklen_t;
#endif

std::string parse_error(int errNo)
{
    std::array<char, 256> tmp{0};
    strerror_r(errNo, tmp.data(), tmp.size());
    return {tmp.data()};
}

///@brief Some possible configurations here
namespace {
///@brief Should properly initialize WSA on Windows.
[[maybe_unused]]
const WSAInitializer initializeWSA_;

///@brief Maximum outstanding connection requests
constexpr int MAXPENDING = 15;

///@brief How often poll/accept timeouts so thread can be interrupted. Too fast raises chances to
/// miss clients' connections.
constexpr std::chrono::milliseconds kAcceptPollTimeout(500);
} // namespace

constexpr CManagedFd::CManagedFd() noexcept :
    fd(kNoSocket)
{
}

constexpr CManagedFd::CManagedFd(socketfd_t value) noexcept :
    fd(value)
{
}

CManagedFd::CManagedFd(CManagedFd &&other) noexcept :
    fd(other.fd.exchange(kNoSocket))
{
}

CManagedFd::~CManagedFd()
{
    auto val = fd.exchange(kNoSocket);
    if (is_valid_socket_fd(val))
    {
#if defined(_MSC_VER)
        ::closesocket(val);
#else
        ::close(val);
#endif
    }
}
CManagedFd &CManagedFd::operator=(CManagedFd &&other) noexcept
{
    if (this != &other)
    {
        fd.store(other.fd.exchange(kNoSocket));
    }
    return *this;
}

CManagedFd::operator socketfd_t() const noexcept
{
    return get();
}

socketfd_t CManagedFd::get() const noexcept
{
    return fd.load();
}

// NOLINTNEXTLINE
CClientSocket::CClientSocket() noexcept
{
    close();
}

CClientSocket::CClientSocket(CManagedFd sockfd, sockaddr_in sock_addr) noexcept :
    m_sockfd(std::move(sockfd)),
    m_sockaddr_in(sock_addr)
{
}

CClientSocket::~CClientSocket() noexcept
{
    close();
}

void CClientSocket::close() noexcept
{
    m_sockfd = CManagedFd();
    memset(&m_sockaddr_in, 0, sizeof(m_sockaddr_in));
}

CClientSocket::Result CClientSocket::write(const std::string &what) const
{
    return write_all(what.c_str(), what.length());
}

CClientSocket::Result CClientSocket::write_all(const void *_buf,
                                               std::size_t size_buf) const noexcept
{
    //::send
    // http://man7.org/linux/man-pages/man2/send.2.html
    // The system calls send(), sendto(), and sendmsg() are used to transmit
    // a message to another socket.

    const char *buf = static_cast<const char *>(_buf); // can't do pointer arithmetic on void*
    EIoStatus res = EIoStatus::Ok;
    std::size_t size_left = size_buf;
    for (; size_left > 0;)
    {
        static const int kFlags = 0;
        auto sent_size = ::send(m_sockfd, buf, size_left, kFlags);
        static_assert(std::is_signed_v<decltype(sent_size)>);

        if (sent_size < 0)
        {
            std::cerr << "send error: " << parse_error(errno) << std::endl;
            res = EIoStatus::Error;
            break;
        }
        size_left -= sent_size;
        std::advance(buf, sent_size);
    }
    return {res, size_left};
}

CClientSocket::Result CClientSocket::read_all(void *_buf, std::size_t size_buf) const noexcept
{
    // http://man7.org/linux/man-pages/man2/recv.2.html
    // The recv(), recvfrom(), and recvmsg() calls are used to receive
    // messages from a socket.
    // NOTE: assumes : 1) blocking socket 2) socket closed , that makes ::recv return 0

    char *buf = static_cast<char *>(_buf); // can't do pointer arithmetic on void*
    std::size_t total_recv_size = 0;
    EIoStatus res = EIoStatus::Ok;
    for (std::size_t size_left = size_buf; size_left > 0;)
    {
        static const int kFlags = 0;
        auto recv_size = ::recv(m_sockfd, buf, size_left, kFlags);
        static_assert(std::is_signed_v<decltype(recv_size)>);

        if (recv_size < 0)
        {
            std::cerr << "recv error: " << parse_error(errno) << std::endl;
            res = EIoStatus::Error;
            break;
        }
        // everything received, exit
        if (0 == recv_size)
        {
            break;
        }
        size_left -= recv_size;
        total_recv_size += recv_size;
        std::advance(buf, recv_size);
    }
    return {res, total_recv_size};
}

std::list<std::string> CClientSocket::hostname_to_ip(const char *host_name,
                                                     const EIpType type) noexcept
{
    // The getaddrinfo function provides protocol-independent translation from an ANSI host name
    // to an address.

    std::list<std::string> result;

    struct addrinfo hints{}, *servinfo = nullptr;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host_name, "http", &hints, &servinfo) != 0)
    {
        std::cerr << "resolving error: " << parse_error(errno) << std::endl;
        return result;
    }

    const bool get_v4 = type == EIpType::IPv4 || type == EIpType::Both;
    const bool get_v6 = type == EIpType::IPv6 || type == EIpType::Both;

    try
    {
        std::array<char, INET_ADDRSTRLEN + 1u> buffer{};
        for (auto p = servinfo; p != nullptr; p = p->ai_next)
        {
            buffer.fill(0);
            if (p->ai_family == AF_INET && get_v4)
            {
                if (inet_ntop(AF_INET, &(copy_cast<sockaddr_in *>(p->ai_addr))->sin_addr,
                              buffer.data(), buffer.size()))
                {
                    result.emplace_back(buffer.data());
                }
                continue;
            }
            if (p->ai_family == AF_INET6 && get_v6)
            {
                if (inet_ntop(AF_INET6, &(copy_cast<sockaddr_in6 *>(p->ai_addr))->sin6_addr,
                              buffer.data(), buffer.size()))
                {
                    result.emplace_back(buffer.data());
                }
                continue;
            }
        }
    }
    catch (...) // NOLINT
    {
        std::cerr << "Unknown Exception doing resolving." << std::endl;
    }

    freeaddrinfo(servinfo);
    return result;
}

CTcpAcceptServer::CTcpAcceptServer(const uint16_t server_port) :
    listenFd{::socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)}
{
    if (!listenFd)
    {
        throw std::runtime_error("Could not create server TCP socket.");
    }

    // allow socket descriptor to be reuseable
    int on = 1;
    if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
    {
        throw std::runtime_error("Could not set options on server TCP socket.");
    }

    // construct local address structure
    sockaddr_in server_addr{};                       // local address
    memset(&server_addr, 0, sizeof(server_addr));    // zero out structure
    server_addr.sin_family = AF_INET;                // internet address family
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // any incoming interface
    server_addr.sin_port = htons(server_port);       // local port

    // bind to the local address
    if (::bind(listenFd, copy_cast<const sockaddr *>(&server_addr), sizeof(server_addr)) < 0)
    {
        // bind error: Permission denied
        // probably trying to bind a port under 1024. These ports usually require root privileges to
        // be bound.
        throw std::runtime_error("Could not bind server TCP socket.");
    }

    // mark the socket so it will listen for incoming connections
    if (::listen(listenFd, MAXPENDING) < 0)
    {
        throw std::runtime_error("Could listen server TCP socket.");
    }
}

CClientSocket CTcpAcceptServer::accept()
{
    sockaddr_in addr_client{}; // client address
    addr_len_t len_addr = sizeof(addr_client);

    // wait for a client to connect
    CManagedFd client_socket(::accept(listenFd, copy_cast<sockaddr *>(&addr_client), &len_addr));
    if (client_socket)
    {
        return {std::move(client_socket), addr_client};
    }
    std::cerr << "Accept Error: " << parse_error(errno) << std::endl;
    return {};
}

CClientSocket CTcpAcceptServer::accept_autoclose(const utility::runnerint_t &is_interrupted_ptr)
{
    sockaddr_in addr_client{}; // client address
    addr_len_t len_addr = sizeof(addr_client);

    // set of socket descriptors
    // NOLINTNEXTLINE
    std::array<pollfd, 1u> fds = {
      pollfd{listenFd, POLLIN, 0}, // NOLINT
    };

    while (!(*is_interrupted_ptr))
    {
        // NOLINTNEXTLINE
        if (poll(fds.data(), fds.size(), kAcceptPollTimeout.count()) < 1
            || fds.front().revents != POLLIN)
        {
            continue;
        }
        fds.front().revents = 0;

        // wait for a client to connect
        CManagedFd client_socket(
          ::accept(listenFd, copy_cast<sockaddr *>(&addr_client), &len_addr));
        if (client_socket)
        {
            return {std::move(client_socket), addr_client};
        }
        std::cerr << "Accept Error: " << parse_error(errno) << std::endl;
    }
    return {};
}

CTcpClientConnecction::CTcpClientConnecction(const char *host_name, const std::uint16_t server_port) :
    m_server_port(server_port),
    m_server_ip(CClientSocket::hostname_to_ip(host_name, EIpType::IPv4))
{
}

EIoStatus CTcpClientConnecction::connect(const char *host_name, const uint16_t server_port)
{
    // I'm not sure if existing design will work for IPv6, so let it be fixed V4.
    m_server_ip = CClientSocket::hostname_to_ip(host_name, EIpType::IPv4);
    m_server_port = server_port;
    return connect();
}

EIoStatus CTcpClientConnecction::connect() noexcept
{
    disconnect();
    // create a stream socket using TCP
    CManagedFd fd(::socket(PF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!fd)
    {
        std::cerr << "Client Socket Error: " << parse_error(errno) << std::endl;
        return EIoStatus::Error;
    }

    // construct the server address structure
    sockaddr_in server_addr{}; // server address
    for (const auto &ip : m_server_ip)
    {
        memset(&server_addr, 0, sizeof(server_addr)); // zero out structure
        server_addr.sin_family = AF_INET;             // internet address family
        if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0)
        {
            continue;
        }

        server_addr.sin_port = htons(m_server_port); // server port

        // establish the connection to the server
        if (::connect(fd, copy_cast<sockaddr *>(&server_addr), sizeof(server_addr)) < 0)
        {
            return EIoStatus::Error;
        }
        m_client_socket = {std::move(fd), server_addr};
        return EIoStatus::Ok;
    }

    std::cerr << "inet_pton error: " << parse_error(errno) << std::endl;
    return EIoStatus::Error;
}

void CTcpClientConnecction::disconnect() noexcept
{
    m_client_socket.close();
}
