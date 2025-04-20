#pragma once

#include <ollama/ollama.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Words which ollama will pass to us asking for the help.
using TAssistWords = std::vector<std::string_view>;

///@brief This objects tries to recognize beginning of the string in chunked data.
class CContentRestorator
{
  public:
    // Keep reading ollama
    struct TNeedMoreData
    {
    };

    // Nothing found, pass to user what we've collected so far.
    struct TPassToUser
    {
        std::string collectedString;
    };

    // Something detected.
    struct TDetected
    {
        const std::string_view &whatDetected;
        std::string collectedString;
    };

    // Decisiong was returned already before. Call .Reset() to start detection again.
    struct TAlreadyDetected
    {
    };

    using TDecision = std::variant<TNeedMoreData, TPassToUser, TDetected, TAlreadyDetected>;

  public:
    explicit CContentRestorator(TAssistWords aWhatToLookFor);

    void Reset();
    TDecision Update(const ollama::response &respFromOllama);

  private:
    TAssistWords whatToLookFor;
    std::string lastData;
    bool detectionHappened{false};
};
