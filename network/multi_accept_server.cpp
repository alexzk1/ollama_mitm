#include "multi_accept_server.hpp" // IWYU pragma: keep

#include "socket.hpp" // IWYU pragma: keep

#include <common/runners.h>
#include <common/threads_pool.hpp>

#include <cstdint>
#include <memory>
#include <utility>

void CTcpServer::listen(const uint16_t aPort, TClientHandler aClientHandler)
{
    const auto listenThreadBody = [handler = std::move(aClientHandler),
                                   aPort](const utility::runnerint_t &aStopListen) {
        utility::CThreadPool threadPool;
        CTcpAcceptServer acceptServer(aPort);

        while (!(*aStopListen))
        {
            // We have to make shared_ptr because std::function<> inside CThreadPool wants copyable
            // lambda.
            if (auto clientSocket =
                  std::make_shared<CClientSocket>(acceptServer.accept_autoclose(aStopListen)))
            {
                if (*clientSocket)
                {
                    threadPool.enqueue([clientSocket, handler](const auto &aClientStopper) mutable {
                        handler(aClientStopper, std::move(*clientSocket));
                    });
                }
            }
        }
    };
    iListenThread = utility::startNewRunner(listenThreadBody);
}

void CTcpServer::stop()
{
    iListenThread.reset();
}
