#pragma once

#include <commands/ollama_commands.hpp>
#include <ollama/ollama.hpp>

#include <optional>
#include <string>
#include <variant>
#include <vector>

// Words which ollama will pass to us asking for the help.
using TAssistWords = std::vector<std::string>;

///@brief This objects tries to recognize beginning of the string in chunked data.
/// If it is recognized it keeps consuming input and returns whole full message.
class CContentRestorator
{
  public:
    enum class EReadingBehahve {
        OllamaHasMore,
        OllamaSentAll,
        CommunicationFailure,
    };

    // Keep reading ollama
    struct TNeedMoreData
    {
        EReadingBehahve status;
        const std::string &currentlyCollectedString;
    };

    // Nothing found, pass to user what we've collected so far. Keep to pass anything else.
    struct TPassToUser
    {
        EReadingBehahve status;
        std::string collectedString;
    };

    // Something detected. collectedString is fully composed ollama's text.
    struct TDetected
    {
        EReadingBehahve status;
        const std::string &whatDetected;
        std::string collectedString;
    };

    // Decisiong was returned already before. Call .Reset() to start detection again.
    struct TAlreadyDetected
    {
        EReadingBehahve status;
    };

    using TDecision = std::variant<TNeedMoreData, TPassToUser, TDetected, TAlreadyDetected>;

  public:
    using TUpdateResult = std::pair<EReadingBehahve, TDecision>;

    explicit CContentRestorator(const TAiCommands &aWhatToLookFor);
    explicit CContentRestorator(TAssistWords aWhatToLookFor);

    /// @brief Resets the state of the detector. Can be called after TAlreadyDetected is returned to
    /// reuse object again.
    void Reset();

    /// @returns Current state of the response composition.
    TUpdateResult Update(const ollama::response &respFromOllama);

    /// @brief Tries to parse boolean value of the "done" field and @returns it.
    /// @returns std::nullopt if "done" field is not present or cannot be parsed.
    static std::optional<bool> IsModelDone(const ollama::response &respFromOllama);

  private:
    using TStorage = TAssistWords;
    TStorage whatToLookFor;
    std::optional<TStorage::const_iterator> lastDetected;
    std::string lastData;

    [[nodiscard]]
    bool IsPassToUser() const
    {
        return lastDetected.has_value() && whatToLookFor.cend() == *lastDetected;
    }

    [[nodiscard]]
    bool IsDetected() const
    {
        return lastDetected.has_value() && whatToLookFor.cend() != *lastDetected;
    }

    TUpdateResult AllReceivedResult();
};
