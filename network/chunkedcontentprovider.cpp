#include "chunkedcontentprovider.hpp" // IWYU pragma: keep

#include <common/lambda_visitors.h>
#include <network/contentrestorator.hpp>
#include <network/ollama_proxy_config.hpp>
#include <ollama/httplib.h>
#include <ollama/json.hpp>
#include <ollama/ollama.hpp>

#include <cstddef>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

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
        sink.write(what.c_str(), what.size());
    }
}

void WriteToSink(httplib::DataSink &sink, const ollama::response &ollamaResponse)
{
    WriteToSink(sink, ollamaResponse.as_json_string());
}

constexpr std::initializer_list<std::string_view> kOllamaKeywords{"AI_GET_URL"};

} // namespace

CChunkedContentProvider::CChunkedContentProvider(const httplib::Request &userRequest,
                                                 const TOllamaProxyConfig &proxyConfig) :
    ollamaServer(proxyConfig.CreateOllamaUrl()),
    parsedUserJson(nlohmann::json::parse(userRequest.body)),
    userRequest(userRequest),
    proxyConfig(proxyConfig)
{
    constexpr auto kStreamKey = "stream";

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
}

bool CChunkedContentProvider::operator()(std::size_t /*offset*/, httplib::DataSink &sink)
{
    proxyConfig.get().ExecIfFittingVerbosity(EOllamaProxyVerbosity::Debug, [this](auto &os) {
        os << "[DEBUG] CChunkedContentProvider::operator(), we have stored request to process: \n"
           << parsedUserJson << std::endl;
    });
    try
    {
        if (!sink.is_writable())
        {
            proxyConfig.get().ExecIfFittingVerbosity(EOllamaProxyVerbosity::Warning, [](auto &os) {
                os << "[WARNING] Sink is not writable even before asking Ollama." << std::endl;
            });
            return false;
        }
        auto commandDetector = std::make_shared<CContentRestorator>(kOllamaKeywords);
        const auto ollamaResponseHandler = [&sink, commandDetector = std::move(commandDetector),
                                            this](const ollama::response &ollamaResponse) -> bool {
            // We should return true/false from callback to ollama server, AND stop sink if
            // we're done, otherwise client will keep repeating.
            const auto respondToUserAndOllama =
              [&sink](CContentRestorator::EReadingBehahve status) {
                  switch (status)
                  {
                      case CContentRestorator::EReadingBehahve::OllamaHasMore:
                          return true; // Keep talking to Ollama.
                      case CContentRestorator::EReadingBehahve::CommunicationFailure:
                          [[fallthrough]];
                      case CContentRestorator::EReadingBehahve::OllamaSentAll:
                          sink.done(); // Close user's connection.
                          break;
                  }
                  return false; // Finish talks to Ollama.
              };

            const auto writeAsJson = [&](const std::string &plain) {
                auto jobj = ollamaResponse.as_json();
                jobj["message"]["content"] = plain;
                WriteToSink(sink, ollama::response(jobj.dump(), ollama::message_type::chat));
            };

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
                          os
                            << "[WARNING] Response from ollama does not have boolean 'done' field. "
                               "Stopping communications."
                            << ollamaResponse.as_json() << std::endl;
                      });
                    return respondToUserAndOllama(status);
                }

                // Cannot write to user, drop both.
                if (!sink.is_writable())
                {
                    proxyConfig.get().ExecIfFittingVerbosity(
                      EOllamaProxyVerbosity::Warning, [](auto &os) {
                          os << "[WARNING] Sink to user is not writtable but we got some response "
                                "from Ollama yet."
                             << std::endl;
                      });
                    return respondToUserAndOllama(
                      CContentRestorator::EReadingBehahve::CommunicationFailure);
                }

                const LambdaVisitor visitor{
                  [&](const CContentRestorator::TAlreadyDetected &) {
                      WriteToSink(sink, ollamaResponse);
                      return true;
                  },
                  [&](const CContentRestorator::TNeedMoreData &data) {
                      if (data.status == CContentRestorator::EReadingBehahve::OllamaSentAll)
                      {
                          writeAsJson(data.currentlyCollectedString);
                      }
                      return true;
                  },
                  [&](const CContentRestorator::TPassToUser &pass) {
                      writeAsJson(pass.collectedString);
                      return true;
                  },
                  [&](const CContentRestorator::TDetected &) {
                      // Here we have full ollama's response (request by model) to do something,
                      // It must not be sent to user. It must be served, and sent as new request to
                      // ollama than repeat whole ollamaResponseHandler () again.
                      throw std::runtime_error("TODO: not implemented.");
                      return false;
                  },
                };
                if (std::visit(visitor, decision))
                {
                    return respondToUserAndOllama(status);
                }
                // We get here by CContentRestorator::TDetected, which means we close this channel
                // to Ollama, but we keep channel to the user.
                return false;
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

        auto request = CreateChatRequest(parsedUserJson);
        return ollamaServer.chat(request, ollamaResponseHandler);
    }
    catch (std::exception &e)
    {
        proxyConfig.get().ExecIfFittingVerbosity(EOllamaProxyVerbosity::Error, [&e](auto &os) {
            os << "[ERROR] Exception in CChunkedContentProvider: " << e.what() << std::endl;
        });
    }

    return false;
}
