#include "chunkedcontentprovider.hpp" // IWYU pragma: keep

#include <network/contentrestorator.hpp>
#include <network/ollama_proxy_config.hpp>
#include <ollama/httplib.h>
#include <ollama/json.hpp>
#include <ollama/ollama.hpp>

#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

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

const TAssistWords kOllamaKeywords = {
  "AI_GET_URL",
};

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
            const auto tellUserDone = [&sink](bool keepChatWithOllama = false) {
                if (!keepChatWithOllama)
                {
                    sink.done();
                }
                return keepChatWithOllama;
            };
            try
            {
                const auto &jsonObj = ollamaResponse.as_json();
                proxyConfig.get().ExecIfFittingVerbosity(
                  EOllamaProxyVerbosity::Debug, [&jsonObj](auto &os) {
                      os << "[DEBUG] Response from OLLAMA: \n" << jsonObj << std::endl;
                  });

                if (!sink.is_writable())
                {
                    proxyConfig.get().ExecIfFittingVerbosity(
                      EOllamaProxyVerbosity::Warning, [](auto &os) {
                          os << "[WARNING] Sink to user is not writtable but we got some response "
                                "from Ollama yet."
                             << std::endl;
                      });
                    return tellUserDone();
                }

                // commandDetector->Update(ollamaResponse);
                // const auto detectedState = commandDetector->Detect(kTestString);
                // if (detectedState == ECommandDetectionState::SureAbsent)
                // {

                // }

                WriteToSink(sink, ollamaResponse);
                constexpr auto kDoneKey = "done";

                if (!jsonObj.contains(kDoneKey) || !jsonObj[kDoneKey].is_boolean())
                {
                    proxyConfig.get().ExecIfFittingVerbosity(
                      EOllamaProxyVerbosity::Warning, [&jsonObj](auto &os) {
                          os
                            << "[WARNING] Response from ollama does not have boolean 'done' field. "
                               "Stopping communications."
                            << jsonObj << std::endl;
                      });
                    return tellUserDone();
                }
                return tellUserDone(!jsonObj[kDoneKey]);
            }
            catch (std::exception &e)
            {
                proxyConfig.get().ExecIfFittingVerbosity(
                  EOllamaProxyVerbosity::Error, [&e](auto &os) {
                      os << "[ERROR] Exception while handling OLLAMA response: " << e.what()
                         << std::endl;
                  });
            }
            return tellUserDone(false); // Stop streaming to user.
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
