#pragma once

#include "socket.hpp" // IWYU pragma: keep

#include <common/cm_ctors.h>
#include <common/runners.h>
#include <common/threads_pool.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

/// @brief This is multi-threaded TCP server, which can handle multiple clients at the same time.
class CTcpServer
{
  public:
    /// @brief Defines callback to handle client. When passed CClientSocket is destroyed, client is
    /// disconnected.
    using TClientHandler = std::function<void(utility::runnerint_t, CClientSocket)>;

    CTcpServer() = default;
    ~CTcpServer() = default;
    MOVEONLY_ALLOWED(CTcpServer);

    void listen(const std::uint16_t aPort, TClientHandler aClientHandler);
    void stop();

    [[nodiscard]]
    bool isListening() const
    {
        return iListenThread != nullptr;
    }

  private:
    std::shared_ptr<std::thread> iListenThread{nullptr};
};
