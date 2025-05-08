#pragma once

#include <common/cm_ctors.h>
#include <common/safe_queue.h>
#include <network/contentrestorator.hpp>
#include <network/ollama_proxy_config.hpp>
#include <network/user_ping_generator.hpp>
#include <ollama/httplib.h>
#include <ollama/json.hpp>
#include <ollama/ollama.hpp>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>

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
    using TCommandResutl = std::variant<ollama::request, std::string>;

    struct TUserRequest
    {
        explicit TUserRequest(const httplib::Request &request);

        std::reference_wrapper<const httplib::Request> userRequest;
        nlohmann::json parsedUserJson;
    };

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
    };

    class CPinger
    {
      public:
        NO_COPYMOVE(CPinger);
        CPinger() = delete;
        explicit CPinger(const TCommObject &comm) :
            comm(comm) {};
        ~CPinger()
        {
            Finish();
        }

        template <typename... taAny>
        void Ping(taAny &&...any) const
        {
            if (ping)
            {
                comm.SendToUser(ping->GeneratePingResponse(std::forward<taAny>(any)...));
            }
        }

        template <typename... taAny>
        void Finish(taAny &&...any)
        {
            if (ping)
            {
                comm.SendToUser(ping->FinishPingsIfAny(std::forward<taAny>(any)...));
            }
            ping = std::nullopt;
        }

        void Restart(std::string model)
        {
            ping.emplace(std::move(model));
        }

      private:
        const TCommObject &comm;
        std::optional<CUserPingGenerator> ping;
    };

  private:
    std::shared_ptr<std::thread> RunOllamaThread();
    TCommandResutl MakeResponseForOllama(CContentRestorator::TDetected aiCommand,
                                         const CPinger &pingUser) const;
    ollama::request MakeResponseForOllama(std::string plainText) const;
    void MakeCommandsAvailForAi();
    template <typename taAny>
    static auto DebugConvert(taAny anything)
    {
        return std::forward<taAny>(anything);
    }

    static auto DebugConvert(const ollama::response &anything)
    {
        return anything.as_json().dump();
    }

    static auto DebugConvert(const ollama::request &anything)
    {
        return anything.dump();
    }

    template <typename... taAny>
    void DebugDump(taAny &&...anything) const
    {
        proxyConfig.get().ExecIfFittingVerbosity(EOllamaProxyVerbosity::Debug, [&](auto &os) {
            os << "[DEBUG] ";
            ((os << DebugConvert(std::forward<taAny>(anything))), ...);
            os << std::endl;
        });
    }

  private:
    TUserRequest userRequest;
    TCommObject commObject;
    std::reference_wrapper<const TOllamaProxyConfig> proxyConfig;
    std::shared_ptr<std::thread> ollamaThread;
};
