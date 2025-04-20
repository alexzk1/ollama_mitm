#pragma once

#include "ollama_proxy_config.hpp" // IWYU pragma: keep

#include <common/cm_ctors.h>
#include <ollama/httplib.h>
#include <ollama/ollama.hpp>

class COllamaProxyServer
{
  public:
    NO_COPYMOVE(COllamaProxyServer);
    COllamaProxyServer();
    ~COllamaProxyServer() = default;
    explicit COllamaProxyServer(TOllamaProxyConfig config);

    /// @brief Starts the proxy server on a specified port.
    void Start(int listenOnPort);
    /// @brief Stops the proxy server.
    void Stop();

  private:
    /// @brief Installs the necessary HTTP handlers for the proxy server.
    void InstallHandlers();
    /// @brief Creates an HTTP client connected to the Ollama server.
    [[nodiscard]]
    httplib::Client CreateOllamaHttpClient() const;
    /// @brief Creates C++ client Ollama with the provided configuration.
    [[nodiscard]]
    Ollama CreateOllamaObject() const;

    void DefaultProxyEverything(const httplib::Request &request, httplib::Response &response) const;
    void HandlePostApiChat(const httplib::Request &userRequest,
                           httplib::Response &responseToUser) const;

    /// @brief handles incoming user's requests.
    httplib::Server server;
    const TOllamaProxyConfig config;
};
