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
#pragma once

#include "cm_ctors.h"
#include "runners.h" // IWYU pragma: keep

#include <memory.h> //NOLINT
#include <unistd.h> //NOLINT

#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <list>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>

#if defined(_MSC_VER)
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <netinet/in.h>
#endif

// multi platform socket descriptor
#if _WIN32
using socketfd_t = SOCKET;
static_assert(std::is_pointer_v<socketfd_t>);
inline static const socketfd_t kNoSocket = nullptr;
inline bool is_valid_socket_fd(socketfd_t fd)
{
    return fd != nullptr;
}
#else
using socketfd_t = int;
static_assert(std::is_signed_v<socketfd_t>);
inline static const socketfd_t kNoSocket = -1;
inline bool is_valid_socket_fd(socketfd_t fd)
{
    return fd > -1;
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////
// utils
/////////////////////////////////////////////////////////////////////////////////////////////////////

///@brief Analog of bits_cast from C++20.
template <typename taDest, typename taSrc>
taDest copy_cast(taSrc src)
{
    static_assert(sizeof(taSrc) == sizeof(taDest));
    taDest res;
    memcpy(&res, &src, sizeof(res));
    return res;
}

///@brief Thread-safe function to get error message from errno.
std::string parse_error(int errNo);

///@brief Takes ownership of the passed FD.
class CManagedFd
{
  public:
    constexpr CManagedFd() noexcept;
    explicit constexpr CManagedFd(socketfd_t value) noexcept;
    CManagedFd(CManagedFd &&other) noexcept;
    CManagedFd(const CManagedFd &) = delete;
    ~CManagedFd();

    CManagedFd &operator=(const CManagedFd &) = delete;
    CManagedFd &operator=(CManagedFd &&other) noexcept;

    // NOLINTNEXTLINE
    operator socketfd_t() const noexcept;

    [[nodiscard]]
    socketfd_t get() const noexcept;

    explicit operator bool() const noexcept
    {
        return is_valid_socket_fd(get());
    }

  private:
    std::atomic<socketfd_t> fd;
};

inline int close(const CManagedFd &)
{
    throw std::runtime_error("Do not use close on object.");
}

///@brief Status of the IO operation.
enum class EIoStatus : std::uint8_t {
    Ok,
    Error,
};

///@brief Defines what type of IP address you want to get by the resolving host name.
enum class EIpType : std::uint8_t {
    IPv4,
    IPv6,
    Both,
};

///@brief Client's socket which allows read-write operations.
class client_socket_t
{
  public:
    ///@brief 1st field is status of the operation, second - number of bytes written or read.
    using Result = std::tuple<EIoStatus, std::size_t>;

    client_socket_t() noexcept;
    client_socket_t(CManagedFd sockfd, sockaddr_in sock_addr) noexcept;
    ~client_socket_t() noexcept;
    MOVEONLY_ALLOWED(client_socket_t);

    /// @brief Closes socket which is disconnect.
    void close() noexcept;

    /// @brief writes buffer to the socket. Blocks caller thread until finished.
    /// @returns remaining to write bytes amout as 2nd field of the result (0 == all done). 1st
    /// field indicates if it was any error.
    Result write_all(const void *buf, std::size_t size_buf) const noexcept;

    /// @brief reads @p size_buf from the socket. It blocks caller thread until finished.
    /// @returns total_read as 2nd field of the result. 1st field indicates if it was any error.
    /// @note As it is blocking operation you would like to know exact size of the data before
    /// reading, or end-of-data could be signaled by disconnect from other side.
    Result read_all(void *buf, std::size_t size_buf) const noexcept;

    /// @brief Writes a string excluding \0. It blocks caller thread until finished.
    /// @returns Same as @fn write_all(...).
    [[nodiscard]]
    Result write(const std::string &what) const;

    /// @brief resolves host into IPs list as strings.
    /// @param host_name - domain / host to translate to IP. Can be null to receive server's address
    /// usable to bind.
    /// @param type - defines what type of address you want to get.
    static std::list<std::string> hostname_to_ip(const char *host_name,
                                                 const EIpType type) noexcept;

    explicit operator bool() const noexcept
    {
        return static_cast<bool>(m_sockfd);
    }

  private:
    CManagedFd m_sockfd;       // socket descriptor
    sockaddr_in m_sockaddr_in; // client address (used to store return value of server accept())
};

///@brief TCP server which listens on a specific port and accepts incoming connections.
class tcp_server_t
{
  public:
    ///@brief Constructs listening socket on @p server_port. To start accept connections, call
    /// accept*().
    explicit tcp_server_t(const std::uint16_t server_port);
    MOVEONLY_ALLOWED(tcp_server_t);
    ~tcp_server_t() = default;

    /// @brief Blocks caller thread until new client connects.
    /// @returns client socket, it evaluates to false if couldn't connect.
    client_socket_t accept();

    /// @brief Same as accept() except it checks the state of @p is_interrupted_ptr and stops
    /// accepting if it evaluates to false.
    client_socket_t accept_autoclose(const utility::runnerint_t &is_interrupted_ptr);

  private:
    CManagedFd listenFd;
};

///@brief TCP client which connects to a server.
class tcp_client_t
{
  public:
    ///@brief Sets server without connection to.
    tcp_client_t(const char *host_name, const uint16_t server_port);
    tcp_client_t() = default;
    MOVEONLY_ALLOWED(tcp_client_t);
    ~tcp_client_t() = default;

    ///@brief Connects to latest known server, set via ctor or overloaded connect method.
    EIoStatus connect() noexcept;

    ///@brief Sets new server and connects to it.
    EIoStatus connect(const char *host_name, const std::uint16_t server_port);

    ///@brief Disconnects from the server by closing socket. It can be connected back to the same
    /// server using connect().
    /// It is safe to call it multiply times.
    void disconnect() noexcept;

    ///@returns const reference to the socket, which can be connected by prior call to connect(...)
    [[nodiscard]]
    const client_socket_t &socket() const noexcept
    {
        return m_client_socket;
    }

  private:
    std::uint16_t m_server_port{0};
    std::list<std::string> m_server_ip{};
    client_socket_t m_client_socket{};
};
