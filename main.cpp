#include <common/runners.h>
#include <network/ollama_proxy.hpp>
#include <network/ollama_proxy_config.hpp>

#include <atomic>
#include <chrono> // IWYU pragma: keep
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <thread>

namespace {
constexpr int kPort = 12345;

std::atomic<int> retCode{0};
std::shared_ptr<std::thread> proxyServerThread{nullptr};
void HandleSignal(int signum)
{
    std::cout << "Received signal " << signum << ", shutting down..." << std::endl;
    proxyServerThread.reset();
    std::cout << "Proxy server stopped." << std::endl;
    std::exit(retCode); // NOLINT
}
} // namespace

int main()
{
    using namespace std::chrono_literals;

    signal(SIGINT, HandleSignal);
    signal(SIGTERM, HandleSignal);

    proxyServerThread = utility::startNewRunner([](const auto &interruptPtr) {
        try
        {
            COllamaProxyServer server({EOllamaProxyVerbosity::Debug});
            server.Start(kPort);
            while (!(*interruptPtr))
            {
                std::this_thread::sleep_for(500ms); // NOLINT
            }
            server.Stop();
        }
        catch (std::exception &e)
        {
            std::cerr << "Server thread exception: " << e.what() << ". Exiting." << std::endl;
            retCode = 255;
        }
    });

    return retCode;
}
