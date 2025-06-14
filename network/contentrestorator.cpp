#include "contentrestorator.hpp" // IWYU pragma: keep

#include <commands/ollama_commands.hpp>
#include <common/lambda_visitors.h>
#include <ollama/ollama.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {
TAssistWords BuildWordsList(const TAiCommands &aWhatToLookFor)
{
    TAssistWords words;
    words.reserve(aWhatToLookFor.size());
    std::transform(aWhatToLookFor.begin(), aWhatToLookFor.end(), std::back_inserter(words),
                   [](const auto &cmd) {
                       return cmd.first;
                   });
    return words;
}
} // namespace

CContentRestorator::CContentRestorator(const TAiCommands &aWhatToLookFor) :
    CContentRestorator(BuildWordsList(aWhatToLookFor))
{
}

CContentRestorator::CContentRestorator(TAssistWords aWhatToLookFor) :
    whatToLookFor(std::move(aWhatToLookFor)),
    lastDetected(std::nullopt)
{
    // Sort whatToLookFor by string length
    if (whatToLookFor.size() > 1)
    {
        std::sort(whatToLookFor.begin(), whatToLookFor.end(), [](const auto &a, const auto &b) {
            assert(a.size() < 10000);
            assert(b.size() < 10000);
            return a.size() < b.size();
        });
    }
}

void CContentRestorator::Reset()
{
    lastData.clear();
    lastDetected = std::nullopt;
}

std::optional<bool> CContentRestorator::IsModelDone(const ollama::response &respFromOllama)
{
    constexpr auto kDoneKey = "done";
    try
    {
        const auto &jsonObj = respFromOllama.as_json();
        if (jsonObj.contains(kDoneKey) && jsonObj[kDoneKey].is_boolean())
        {
            return jsonObj[kDoneKey].get<bool>();
        }
    }
    catch (...) // NOLINT
    {
    }
    return std::nullopt;
}

CContentRestorator::TUpdateResult CContentRestorator::AllReceivedResult()
{
    if (!IsDetected())
    {
        throw std::runtime_error("Logic flow is broken.");
    }
    const auto &word = **lastDetected; // NOLINT

    // Lock from repeating this callback again by making it "pass-to-user" status,
    // which is cought above.
    lastDetected = whatToLookFor.cend();
    return {EReadingBehahve::OllamaSentAll,
            TDetected{EReadingBehahve::OllamaSentAll, word, std::move(lastData)}};
}

CContentRestorator::TUpdateResult CContentRestorator::Update(const ollama::response &respFromOllama)
{
    const auto isDone = IsModelDone(respFromOllama);
    if (!isDone.has_value())
    {
        return {EReadingBehahve::CommunicationFailure, TAlreadyDetected{}};
    }
    const auto status = *isDone ? EReadingBehahve::OllamaSentAll : EReadingBehahve::OllamaHasMore;

    const auto finalizePassToUser = [&]() {
        lastDetected = whatToLookFor.cend();
        return CContentRestorator::TUpdateResult{status, TPassToUser{status, std::move(lastData)}};
    };

    const auto isRecognizedAndFullReceived = [&status, this]() {
        return IsDetected() && status == EReadingBehahve::OllamaSentAll;
    };

    const auto isRecognizedAndParticularyReceived = [&status, this]() {
        return IsDetected() && status == EReadingBehahve::OllamaHasMore;
    };

    // Early exit if we already signaled to pass to user or nothing to look for.
    if (IsPassToUser() || whatToLookFor.empty())
    {
        return {status, TAlreadyDetected{status}};
    }

    // Actual receive data.
    const auto &str = respFromOllama.as_simple_string();
    lastData.append(str);
    const std::size_t dataSize = lastData.size();

    // Here we have all data stored, recognized word, and ollama signals us - nothing more.
    if (isRecognizedAndFullReceived())
    {
        return AllReceivedResult();
    }

    if (isRecognizedAndParticularyReceived())
    {
        LambdaVisitor visitor{[&](auto) -> CContentRestorator::TUpdateResult {
                                  return {status, TNeedMoreData{status, lastData}};
                              },
                              [&](TThatWasResponseToUser) {
                                  // This is the case when ollama starts initial chatting
                                  // with keyword.
                                  return finalizePassToUser();
                              }};
        const std::string &key = **lastDetected;
        return std::visit(visitor, GetAiCommandsList().at(key)(key, lastData));
    }

    // Find the upper bound for strings that could potentially match. These are those with size less
    // than dataSize. If the last string in whatToLookFor is larger than dataSize, we need more
    // data.
    const auto itEnd = std::upper_bound(whatToLookFor.cbegin(), whatToLookFor.cend(), dataSize,
                                        [](std::size_t size, const std::string_view &str) {
                                            return size < str.size();
                                        });
    // Too short text yet collected.
    const bool notEnoughOfDataToRecognize =
      itEnd == whatToLookFor.cend() && whatToLookFor.back().size() > dataSize;
    if (notEnoughOfDataToRecognize || isRecognizedAndParticularyReceived())
    {
        return {status, TNeedMoreData{status, lastData}};
    }

    // Check some strings which already fit received data.
    for (auto it = whatToLookFor.cbegin(); it != itEnd; ++it)
    {
        if (std::equal(it->cbegin(), it->cend(), lastData.cbegin()))
        {
            lastDetected = it;

            if (isRecognizedAndParticularyReceived())
            {
                return {status, TNeedMoreData{status, lastData}};
            }
            assert(isRecognizedAndFullReceived() && "At this point we must had received all.");
            return AllReceivedResult();
        }
    }

    // We still have some longer strings to check.
    if (itEnd != whatToLookFor.cend())
    {
        return {status, TNeedMoreData{status, lastData}};
    }

    // Longest string was checked. Just pass all following to user.
    return finalizePassToUser();
}
