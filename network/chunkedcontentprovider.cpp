#include "chunkedcontentprovider.hpp" // IWYU pragma: keep

#include <common/lambda_visitors.h>
#include <common/runners.h>
#include <network/contentrestorator.hpp>
#include <network/ollama_proxy_config.hpp>
#include <ollama/httplib.h>
#include <ollama/json.hpp>
#include <ollama/ollama.hpp>

#include <atomic>
#include <chrono> // IWYU pragma: keep
#include <cstddef>
#include <exception>
#include <future>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

using namespace std::chrono_literals;

namespace {

// Protection against library changes which can break our hack.
// Checks if library object defines operator=, if so we cannot use static_cast<> below.
template <typename T>
struct has_own_assignment_from_json
{
  private:
    template <typename U>
    static auto test(U *)
      -> decltype(std::declval<U &>().U::operator=(std::declval<const nlohmann::json &>()),
                  std::true_type());

    template <typename>
    static auto test(...) -> std::false_type;

  public:
    static constexpr bool value = decltype(test<T>(nullptr))::value;
};

/// @returns A new ollama::request chat object with the user's JSON data.
ollama::request CreateChatRequest(const nlohmann::json &userJson)
{
    using namespace ollama;
    request req(message_type::chat);

    static_assert(std::is_base_of_v<nlohmann::json, ollama::request>,
                  "ollama::request must be derived from nlohmann::json");
    static_assert(
      !has_own_assignment_from_json<ollama::request>::value,
      "ollama::request defines its own operator=(const nlohmann::json&) which may override "
      "json base operator=. Code below is invalid.");

    // It is safe hack until 2 assertions above are valid.
    static_cast<nlohmann::json &>(req) = userJson;

    return req;
}

void WriteToSink(httplib::DataSink &sink, const std::string &what)
{
    // TODO: here can be different encoding so length in bytes would be different.
    if (!what.empty())
    {
        try
        {
            sink.write(what.c_str(), what.size());
        }
        catch (std::exception &e)
        {
            std::cerr << "Exception on writing to sink: " << e.what() << std::endl;
        }
    }
}

constexpr std::initializer_list<std::string_view> kOllamaKeywords{"AI_GET_URL"};

} // namespace

CChunkedContentProvider::TUserRequest::TUserRequest(const httplib::Request &request) :
    userRequest(request),
    parsedUserJson(nlohmann::json::parse(request.body))
{
}

CChunkedContentProvider::~CChunkedContentProvider()
{
    proxyConfig.get().ExecIfFittingVerbosity(EOllamaProxyVerbosity::Debug, [](auto &os) {
        os << "[DEBUG] TOllamaLifeHandler: Destructor called, resetting thread." << std::endl;
    });
    commObject.DisconnectAll();
    ollamaThread.reset();
}

CChunkedContentProvider::CChunkedContentProvider(const httplib::Request &userRequest,
                                                 const TOllamaProxyConfig &proxyConfig) :
    userRequest(userRequest),
    proxyConfig(proxyConfig),
    ollamaThread(nullptr),
    commObject()
{
    constexpr auto kStreamKey = "stream";
    const auto &parsedUserJson = this->userRequest.parsedUserJson;

    proxyConfig.ExecIfFittingVerbosity(EOllamaProxyVerbosity::Debug, [&parsedUserJson](auto &os) {
        os << "[DEBUG] CChunkedContentProvider::operator(), we have stored request to process: \n"
           << parsedUserJson << std::endl;
    });

    if (!parsedUserJson.contains(kStreamKey))
    {
        throw std::runtime_error("Expected 'stream' field to be present.");
    }
    if (!parsedUserJson[kStreamKey].is_boolean())
    {
        throw std::runtime_error("Expected 'stream' field to be a boolean.");
    }
    if (parsedUserJson[kStreamKey] == false)
    {
        throw std::runtime_error("Expected 'stream' field to be true.");
    }

    ollamaThread = RunOllamaThread();
}

bool CChunkedContentProvider::operator()(std::size_t /*offset*/, httplib::DataSink &sink)
{
    try
    {
        while (const auto str = commObject.GetStringForUser())
        {
            proxyConfig.get().ExecIfFittingVerbosity(
              EOllamaProxyVerbosity::Debug, [&str](auto &os) {
                  os << "[DEBUG] Callback to write to user, sending: \n" << *str << std::endl;
              });
            if (!sink.is_writable())
            {
                proxyConfig.get().ExecIfFittingVerbosity(
                  EOllamaProxyVerbosity::Warning, [](auto &os) {
                      os << "[WARNING] Sink is not writable even before asking Ollama."
                         << std::endl;
                  });
                commObject.DisconnectAll();
                return false;
            }
            WriteToSink(sink, *str);
        }
        // Keep channel opened to user.
        return true;
    }
    catch (std::exception &e)
    {
        proxyConfig.get().ExecIfFittingVerbosity(EOllamaProxyVerbosity::Error, [&e](auto &os) {
            os << "[ERROR] Exception in CChunkedContentProvider: " << e.what() << std::endl;
        });
    }
    commObject.DisconnectAll();
    return false;
}

std::shared_ptr<std::thread> CChunkedContentProvider::RunOllamaThread()
{
    auto threadedOllama = [&](const auto &shouldStopPtr) {
        // Setup ollama handler, it is representation of 1 ollama response, it can be called
        // multiply times if response is json-chunked.
        auto commandDetector = std::make_shared<CContentRestorator>(kOllamaKeywords);

        const auto ollamaResponseHandler =
          [commandDetector = std::move(commandDetector), this](
            const ollama::response &ollamaResponse,
            std::shared_ptr<std::promise<CContentRestorator::TDetected>> detectionPromise) -> bool {
            // We should return true/false from callback to ollama server, AND stop sink if
            // we're done, otherwise client will keep repeating.
            const auto respondToUserAndOllama = [this](CContentRestorator::EReadingBehahve status) {
                switch (status)
                {
                    case CContentRestorator::EReadingBehahve::OllamaHasMore:
                        return !commObject.IsDisconnected(); // Keep talking to Ollama.
                    case CContentRestorator::EReadingBehahve::CommunicationFailure:
                        [[fallthrough]];
                    case CContentRestorator::EReadingBehahve::OllamaSentAll:
                        commObject.DisconnectAll();
                        break;
                }
                return false; // Finish talks to Ollama.
            };

            const auto writeAsJson = [&](const std::string &plain) {
                auto jobj = ollamaResponse.as_json();
                jobj["message"]["content"] = plain;
                commObject.SendToUser(ollama::response(jobj.dump(), ollama::message_type::chat));
            };

            if (commObject.IsDisconnected())
            {
                return false;
            }

            try
            {
                // Debug logging.
                proxyConfig.get().ExecIfFittingVerbosity(
                  EOllamaProxyVerbosity::Debug, [&ollamaResponse](auto &os) {
                      os << "[DEBUG] Response from OLLAMA: \n"
                         << ollamaResponse.as_json() << std::endl;
                  });

                const auto [status, decision] = commandDetector->Update(ollamaResponse);
                if (status == CContentRestorator::EReadingBehahve::CommunicationFailure)
                {
                    proxyConfig.get().ExecIfFittingVerbosity(
                      EOllamaProxyVerbosity::Warning, [&ollamaResponse](auto &os) {
                          os << "[WARNING] Response from ollama does not have boolean 'done' "
                                "field. "
                                "Stopping communications."
                             << ollamaResponse.as_json() << std::endl;
                      });
                    return respondToUserAndOllama(status);
                }

                const LambdaVisitor visitor{
                  [&](const CContentRestorator::TAlreadyDetected &) {
                      commObject.SendToUser(ollamaResponse);
                      return !commObject.IsDisconnected();
                  },
                  [&](const CContentRestorator::TNeedMoreData &data) {
                      if (data.status == CContentRestorator::EReadingBehahve::OllamaSentAll)
                      {
                          writeAsJson(data.currentlyCollectedString);
                      }
                      return !commObject.IsDisconnected();
                  },
                  [&](const CContentRestorator::TPassToUser &pass) {
                      writeAsJson(pass.collectedString);
                      return !commObject.IsDisconnected();
                  },
                  [&](CContentRestorator::TDetected detected) {
                      // Here we have full ollama's response (request by model) to do
                      // something, It must not be sent to user. It must be served, and sent
                      // as new request to ollama than repeat whole ollamaResponseHandler ()
                      // again.
                      detectionPromise->set_value(std::move(detected));
                      return false;
                  },
                };

                return std::visit(visitor, decision) && respondToUserAndOllama(status);
            }
            catch (std::exception &e)
            {
                proxyConfig.get().ExecIfFittingVerbosity(
                  EOllamaProxyVerbosity::Error, [&e](auto &os) {
                      os << "[ERROR] Exception while handling OLLAMA response: " << e.what()
                         << std::endl;
                  });
            }
            return respondToUserAndOllama(
              CContentRestorator::EReadingBehahve::CommunicationFailure);
        }; // ollamaResponseHandler[]()

        Ollama ollamaServer(proxyConfig.get().CreateOllamaUrl());
        const auto execOllamaRequest = [&ollamaResponseHandler, this,
                                        &ollamaServer](ollama::request request) {
            auto detectionPromise = std::make_shared<std::promise<CContentRestorator::TDetected>>();
            auto fut = detectionPromise->get_future();

            ollamaServer.chat(
              request, [&, detectionPromise = std::move(detectionPromise)](const auto &r) -> bool {
                  return ollamaResponseHandler(r, std::move(detectionPromise));
              });
            return std::move(fut);
        };

        const auto isThreadLoopingYet = [&shouldStopPtr, this]() {
            return !(*shouldStopPtr) && !commObject.IsDisconnected();
        };

        auto request = CreateChatRequest(userRequest.parsedUserJson);
        while (isThreadLoopingYet())
        {
            auto fut = execOllamaRequest(std::move(request));
            request = {};
            while (isThreadLoopingYet())
            {
                if (std::future_status::ready == fut.wait_for(250ms) && isThreadLoopingYet())
                {
                    // Handle request from Ollama, set new value to "request" which would be
                    // handler's result.

                    break;
                }
            }
        }
        commObject.DisconnectAll();
        proxyConfig.get().ExecIfFittingVerbosity(EOllamaProxyVerbosity::Debug, [](auto &os) {
            os << "[DEBUG] Ollama's thread loop ended. " << std::endl;
        });
        std::this_thread::sleep_for(200ms);
    };
    return utility::startNewRunner(std::move(threadedOllama));
}

CChunkedContentProvider::TCommObject::TCommObject() :
    ollamaToUser(std::make_unique<TQueue>()),
    disconnectAll(std::make_unique<std::atomic<bool>>(false))
{
}

void CChunkedContentProvider::TCommObject::SendToUser(std::string what) const
{
    if (!IsDisconnected())
    {
        ollamaToUser->push(std::move(what));
    }
}

void CChunkedContentProvider::TCommObject::SendToUser(const ollama::response &ollamaResponse) const
{
    SendToUser(ollamaResponse.as_json_string());
}

void CChunkedContentProvider::TCommObject::DisconnectAll() const
{
    disconnectAll->store(true);
}

bool CChunkedContentProvider::TCommObject::IsDisconnected() const
{
    return disconnectAll->load();
}

std::optional<std::string> CChunkedContentProvider::TCommObject::GetStringForUser() const
{
    return ollamaToUser->pop();
}
