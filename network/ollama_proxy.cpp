#include "ollama_proxy.hpp" // IWYU pragma: keep

#include "chunkedcontentprovider.hpp" // IWYU pragma: keep
#include "ollama_proxy_config.hpp"    // IWYU pragma: keep

#include <ollama/httplib.h>

#include <cstddef>
#include <exception>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

COllamaProxyServer::COllamaProxyServer(TOllamaProxyConfig config) :
    config{std::move(config)}
{
    if (!this->config.Validate())
    {
        throw std::invalid_argument("Invalid configuration for ollama proxy server passed.");
    }
}

void COllamaProxyServer::Start(int listenOnPort)
{
    InstallHandlers();
    server.listen("0.0.0.0", listenOnPort);
}

void COllamaProxyServer::Stop()
{
    server.stop();
}

void COllamaProxyServer::InstallHandlers()
{
    // Just pass everything to ollama as-is.
    const auto handleAll = [this](const httplib::Request &request, httplib::Response &response) {
        DefaultProxyEverything(request, response);
    };
    server.Post("/api/chat", [this](const auto &req, auto &resp) {
        HandlePostApiChat(req, resp);
    });
    server.Get(R"(/(.+))", handleAll);
    server.Post(R"(/(.+))", handleAll);
    server.Put(R"(/(.+))", handleAll);
    server.Delete(R"(/(.+))", handleAll);
}

httplib::Client COllamaProxyServer::CreateOllamaHttpClient() const
{
    return httplib::Client(config.ollamaHost, config.ollamaPort);
}

Ollama COllamaProxyServer::CreateOllamaObject() const
{
    return Ollama{config.CreateOllamaUrl()};
}

void COllamaProxyServer::DefaultProxyEverything(const httplib::Request &request,
                                                httplib::Response &response) const
{
    // Doing
    config.ExecIfFittingVerbosity(EOllamaProxyVerbosity::Debug, [&request](auto &ostream) {
        ostream << "[DEBUG] DefaultProxyEverything(): " << request.method << " " << request.path
                << std::endl;
        for (const auto &header : request.headers)
        {
            ostream << "[DEBUG] \tHeader from user " << header.first << ": " << header.second
                    << std::endl;
        }
        // ostream << "[DEBUG] \tBody from user: " << request.body << std::endl;
    });

    auto httpOllamaCli = CreateOllamaHttpClient();
    httpOllamaCli.set_follow_location(true); // follow redirects
    httplib::Error error{httplib::Error::Unknown};
    httplib::Request copy = request;
    httpOllamaCli.send(copy, response, error);

    if (httplib::Error::Success != error)
    {
        response.status = 502;
    }
}

// Handles POST /api/chat. This is a special case because it requires streaming responses back to
// the client and we will handle userRequest to the web here.
void COllamaProxyServer::HandlePostApiChat(const httplib::Request &userRequest,
                                           httplib::Response &responseToUser) const
{
    config.ExecIfFittingVerbosity(EOllamaProxyVerbosity::Debug, [&userRequest](auto &ostream) {
        ostream << "[DEBUG] HandlePostApiChat(): " << userRequest.method << " " << userRequest.path
                << std::endl;
        for (const auto &header : userRequest.headers)
        {
            ostream << "[DEBUG] \tHeader from user " << header.first << ": " << header.second
                    << std::endl;
        }
    });

    responseToUser.status = 504;
    responseToUser.body = "Invalid content type. Expected application/json from user.";
    static constexpr auto kHeaderKey = "content-type";
    if (userRequest.has_header(kHeaderKey)
        && userRequest.get_header_value(kHeaderKey) == "application/json")
    {
        try
        {
            responseToUser.status = 200;
            responseToUser.body = "";

            // This is last one, now control is moved to the chunked content provider which can
            // "write" only to the user or disconnect.
            auto ptr = std::make_shared<CChunkedContentProvider>(userRequest, config);
            httplib::ContentProviderWithoutLength contentProvider =
              [ptr = std::move(ptr)](size_t offset, httplib::DataSink &sink) {
                  return (*ptr)(offset, sink);
              };
            responseToUser.set_chunked_content_provider("application/json",
                                                        std::move(contentProvider));
        }
        catch (std::exception &e)
        {
            responseToUser.status = 504;
            responseToUser.body = "Invalid JSON. Error: " + std::string(e.what());

            config.ExecIfFittingVerbosity(EOllamaProxyVerbosity::Error, [&e](auto &ostream) {
                ostream << "[ERROR] HandlePostApiChat() exception: " << e.what() << std::endl;
            });
        }
    }

    config.ExecIfFittingVerbosity(EOllamaProxyVerbosity::Debug, [&responseToUser](auto &ostream) {
        ostream << "[DEBUG] Exit HandlePostApiChat(), status:  " << responseToUser.status
                << ", body: " << responseToUser.body << std::endl;
    });
}
