#pragma once

#include <common/cm_ctors.h>
#include <common/safe_queue.h>
#include <network/ollama_proxy_config.hpp>
#include <ollama/httplib.h>
#include <ollama/json.hpp>
#include <ollama/ollama.hpp>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <thread>

class CChunkedContentProvider
{
  public:
    CChunkedContentProvider() = delete;
    ~CChunkedContentProvider();
    MOVEONLY_ALLOWED(CChunkedContentProvider);

    explicit CChunkedContentProvider(const httplib::Request &userRequest,
                                     const TOllamaProxyConfig &proxyConfig);
    bool operator()(std::size_t offset, httplib::DataSink &sink);

  private:
    std::shared_ptr<std::thread> RunOllamaThread();

    struct TUserRequest
    {
        explicit TUserRequest(const httplib::Request &request);

        std::reference_wrapper<const httplib::Request> userRequest;
        nlohmann::json parsedUserJson;
    } userRequest;
    std::reference_wrapper<const TOllamaProxyConfig> proxyConfig;

    std::shared_ptr<std::thread> ollamaThread;

    class TCommObject
    {
      public:
        TCommObject();

        // Used by Ollama thread.
        void SendToUser(std::string what) const;
        void SendToUser(const ollama::response &ollamaResponse) const;

        void DisconnectAll() const;
        [[nodiscard]]
        bool IsDisconnected() const;

        [[nodiscard]]
        std::optional<std::string> GetStringForUser() const;

      private:
        using TQueue = SafeQueue<std::string>;
        std::unique_ptr<TQueue> ollamaToUser;
        std::unique_ptr<std::atomic<bool>> disconnectAll;
    } commObject;
};
