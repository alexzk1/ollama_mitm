#include "chunkedcontentprovider.hpp" // IWYU pragma: keep

#include <commands/ollama_commands.hpp>
#include <common/lambda_visitors.h>
#include <common/runners.h>
#include <network/contentrestorator.hpp>
#include <network/ollama_proxy_config.hpp>
#include <ollama/httplib.h>
#include <ollama/json.hpp>
#include <ollama/ollama.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono> // IWYU pragma: keep
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
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

void ReplaceSubstring(std::string &str, const std::string &from, const std::string &to)
{
    size_t index = 0;
    while (true)
    {
        // find substring
        index = str.find(from, index);
        if (index == std::string::npos)
        {
            break;
        }

        // relace it with target to
        str.replace(index, from.length(), to);

        // jump further to after replacement
        index += to.length();
    }
}

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
    commObject(),
    proxyConfig(proxyConfig),
    ollamaThread(nullptr)
{
    constexpr auto kStreamKey = "stream";
    auto &parsedUserJson = this->userRequest.parsedUserJson;

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

    MakeCommandsAvailForAi();

    proxyConfig.ExecIfFittingVerbosity(EOllamaProxyVerbosity::Debug, [&parsedUserJson](auto &os) {
        os << "[DEBUG] CChunkedContentProvider::operator(), we have stored request to process: \n"
           << parsedUserJson << std::endl;
    });

    ollamaThread = RunOllamaThread();
}

void CChunkedContentProvider::MakeCommandsAvailForAi()
{
    auto &parsedUserJson = this->userRequest.parsedUserJson;
    auto &msgs = parsedUserJson["messages"];

    auto it = std::adjacent_find(msgs.begin(), msgs.end(), [](const auto &ja, const auto &jb) {
        return ja["role"] != jb["role"] && ja["role"] == "system";
    });
    std::advance(it, 1);
    if (it == msgs.end())
    {
        it = msgs.begin();
    }

    std::ostringstream fullList;

    fullList << "There is (are) backend keyword(s) below you can you to access real world.\nPut "
                "keyword as first word in reply to receive real world information\nPrepend keyword "
                "with any words or symbols to send it to user.\n";
    fullList << "\n\n";
    for (const auto &aiCommand : proxyConfig.get().GetAiCommands())
    {
        std::string text = aiCommand.second.instructionForAi;
        ReplaceSubstring(text, "${KEYWORD}", aiCommand.first);
        fullList << text << "\n\n";
    }

    fullList << "List of keywords is ended.\n\n";

    nlohmann::json js;
    js["content"] = fullList.str();
    js["role"] = "system";
    msgs.insert(it, std::move(js));
}

bool CChunkedContentProvider::operator()(std::size_t /*offset*/, httplib::DataSink &sink)
{
    // This is communication to the user, called by server wrapper pereodically.
    try
    {
        while (const auto what = commObject.GetStringForUser())
        {
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
            if (!what->empty())
            {
                try
                {
                    std::ostringstream oss;
                    oss << std::hex << what->size() << "\r\n" // размер чанка в hex
                        << *what << "\r\n";                   // сами данные
                    const std::string chunk = oss.str();
                    // const auto &chunk = *what;
                    DebugDump("operator() to write to user, sending\n", chunk,
                              "\n\tOf size: ", chunk.size());
                    sink.write(chunk.c_str(), chunk.size());
                }
                catch (std::exception &e)
                {
                    proxyConfig.get().ExecIfFittingVerbosity(
                      EOllamaProxyVerbosity::Error, [&e](auto &os) {
                          os << "[ERROR] Error writing to user: \n" << e.what() << std::endl;
                      });
                }
            }
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
    DebugDump("Finishing CChunkedContentProvider::operator() with false.");
    commObject.DisconnectAll();
    return false;
}

std::shared_ptr<std::thread> CChunkedContentProvider::RunOllamaThread()
{
    auto threadedOllama = [&](const auto &shouldStopPtr) {
        // Setup ollama handler, it is representation of 1 ollama response, it can be called
        // multiply times if response is json-chunked.
        const auto commandDetector =
          std::make_shared<CContentRestorator>(proxyConfig.get().GetAiCommands());

        CPinger pingGen(commObject);
        const auto ollamaResponseHandler =
          [commandDetector = commandDetector, this, &pingGen](
            const ollama::response &ollamaResponse,
            std::shared_ptr<std::promise<CContentRestorator::TDetected>> detectionPromise) -> bool {
            // We should return true/false from callback to ollama server, AND stop sink if
            // we're done, otherwise client will keep repeating.
            const auto respondToUserAndOllama =
              [this, &pingGen](CContentRestorator::EReadingBehahve status) {
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

            const auto sendPlainTextToUser = [&](const std::string &plain) {
                pingGen.Finish();
                const auto resp = CUserPingGenerator::ReplaceOllamaText(ollamaResponse, plain);
                DebugDump("writeAsJson:", resp);
                commObject.SendToUser(resp);
            };

            if (commObject.IsDisconnected())
            {
                return false;
            }

            try
            {
                DebugDump("Real Ollama's Answer:", ollamaResponse);
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

                // Parsed commulated response from Ollama.
                const LambdaVisitor visitor{
                  [&](const CContentRestorator::TAlreadyDetected &) {
                      DebugDump("CContentRestorator::TAlreadyDetected", ollamaResponse,
                                "\n\tIsEmpty: ", ollamaResponse.as_json().dump().empty());
                      commObject.SendToUser(ollamaResponse);
                      return !commObject.IsDisconnected();
                  },
                  [&](const CContentRestorator::TNeedMoreData &data) {
                      DebugDump("CContentRestorator::TNeedMoreData");
                      if (data.status == CContentRestorator::EReadingBehahve::OllamaSentAll)
                      {
                          DebugDump("\tCContentRestorator::EReadingBehahve::OllamaSentAll");
                          sendPlainTextToUser(data.currentlyCollectedString);
                      }
                      return !commObject.IsDisconnected();
                  },
                  [&](const CContentRestorator::TPassToUser &pass) {
                      DebugDump("CContentRestorator::TPassToUser");
                      sendPlainTextToUser(pass.collectedString);
                      return !commObject.IsDisconnected();
                  },
                  [&](CContentRestorator::TDetected detected) {
                      DebugDump("CContentRestorator::TDetected");
                      // Here we have full ollama's response (request by model) to do
                      // something, It must not be sent to user. It must be served, and sent
                      // as new request to ollama than repeat whole ollamaResponseHandler ()
                      // again.
                      detectionPromise->set_value(std::move(detected));
                      return false;
                  },
                };

                auto isCont = std::visit(visitor, decision) && respondToUserAndOllama(status);
                DebugDump("IsContinue to read ollama: ", isCont);
                return isCont;
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
        CAiLoopDetector loopDetector;
        while (isThreadLoopingYet())
        {
            if (request.empty())
            {
                break;
            }
            const auto model = userRequest.parsedUserJson["model"];
            pingGen.Restart(model);
            auto fut = execOllamaRequest(std::move(request));
            request = {};
            while (isThreadLoopingYet())
            {
                if (std::future_status::ready == fut.wait_for(250ms) && isThreadLoopingYet())
                {
                    // Handle request from Ollama, set new value to "request" which would be
                    // handler's result.

                    CContentRestorator::TDetected aiCommand = std::move(fut.get());
                    DebugDump("Received request from AI to do something:", aiCommand.whatDetected);

                    const LambdaVisitor visitor{
                      [&](std::string responseForUser) {
                          loopDetector.Reset();
                          auto json = CUserPingGenerator::BuildJsStringForUser(
                            model, std::move(responseForUser));
                          pingGen.Finish();
                          DebugDump("We have response for user:\n", json);
                          commObject.SendToUser(std::move(json));
                          std::this_thread::sleep_for(150ms);
                      },
                      [&, cmd = aiCommand.whatDetected](ollama::request forOllama) {
                          loopDetector.Update(cmd);
                          if (loopDetector.IsLooping())
                          {
                              forOllama =
                                MakeResponseForOllama("You request cannot produce more data than "
                                                      "you already got. Stop repeating it.");
                          }
                          DebugDump("Sending back to AI\n", request);
                          request = std::move(forOllama);
                          commandDetector->Reset();
                      },
                    };
                    std::visit(visitor, MakeResponseForOllama(std::move(aiCommand), pingGen));
                    break;
                }
            }
            DebugDump("Finished inner loop of Ollaming...");
            pingGen.Finish();
        }
        DebugDump("Finished outer loop of Ollaming...");
        pingGen.Finish();
        commObject.DisconnectAll();
        std::this_thread::sleep_for(200ms);
    };
    // Warning! It is tempting to use pool, but than we need to be sure this object exists until
    // lambda exists in pool.
    return utility::startNewRunner(std::move(threadedOllama));
}

ollama::request CChunkedContentProvider::MakeResponseForOllama(std::string plainText) const
{
    auto clone = userRequest.parsedUserJson;
    nlohmann::json js;
    js["role"] = "user";
    plainText.append("\n");
    DebugDump("Backend response to ollama:\n", plainText);
    js["content"] = std::move(plainText);
    clone["messages"].emplace_back(std::move(js));
    return CreateChatRequest(clone);
}

CChunkedContentProvider::TCommandResutl
CChunkedContentProvider::MakeResponseForOllama(CContentRestorator::TDetected aiCommand,
                                               const CPinger &pingUser) const
{
    const auto &commands = proxyConfig.get().GetAiCommands();
    const auto it = commands.find(aiCommand.whatDetected);

    // Ollama asked us to do something. Do it.
    if (it == commands.end())
    {
        return MakeResponseForOllama("Backend failure. This request cannot be processed now.");
    }

    const LambdaVisitor visitor{
      [&](TThatWasResponseToUser resp) -> TCommandResutl {
          return std::move(resp.originalOllamaAnswer);
      },
      [&](ThatWasRequestToFulfill resp) -> TCommandResutl {
          pingUser.Ping();
          return MakeResponseForOllama(std::move(resp.computedValueForOllama));
      },
      [&](TProbablyThatWasResponseToUser resp) -> TCommandResutl {
          // FIXME: revise here, when we come to URLs etc.
          pingUser.Ping();
          return MakeResponseForOllama(std::move(resp.computedValueForOllama));
      },
    };
    pingUser.Ping();
    return std::visit(visitor,
                      it->second.resultProvider(it->first, std::move(aiCommand.collectedString)));
}

CChunkedContentProvider::TCommObject::TCommObject() :
    ollamaToUser(std::make_unique<TQueue>()),
    disconnectAll(std::make_unique<std::atomic<bool>>(false))
{
}

void CChunkedContentProvider::TCommObject::SendToUser(std::string what) const
{
    if (!IsDisconnected() && !what.empty())
    {
        ollamaToUser->push(std::move(what));
    }
}

void CChunkedContentProvider::TCommObject::SendToUser(const ollama::response &ollamaResponse) const
{
    SendToUser(ollamaResponse.as_json().dump());
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

/*
Say only keyword in response AI_DATE_TIME_NOW As result you must get date
time. Than say keyword AI_DATE_TIME_NOW again. You must get date time again. Compare them, it should
be less than 1 minute between passed. Tell me final result of comparison only.
 */
