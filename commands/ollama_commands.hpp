#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>

/// @brief Model responded to user for sure. Ignore computed value and send source to user instead.
struct TThatWasResponseToUser
{
    std::string originalOllamaAnswer;
};

/// @brief  Model requested for sure. Use computed value and send it to the model.
struct ThatWasRequestToFulfill
{
    std::string computedValueForOllama;
};

/// @brief We're not 100% sure what was that, just in case here is computed value for the model.
struct TProbablyThatWasResponseToUser
{
    std::string computedValueForOllama;
};

/// @brief Models may repeat keyword in answer to user, which will cause infinite loop.
/// This is the heuristical response to try to figure out what model actually wanted.
using TResponseToOllama =
  std::variant<TThatWasResponseToUser, ThatWasRequestToFulfill, TProbablyThatWasResponseToUser>;

/// @brief Describes single command from AI to backend.
struct TAiCommand
{

    TResponseToOllama operator()(const std::string &keyword,
                                 std::string complete_request_from_ollama) const
    {
        return resultProvider(keyword, complete_request_from_ollama);
    };

    /// @brief Instructions to pass to the AI.
    std::string instructionForAi;
    /// @brief Functor which does actual job. Returns plain result, NOT wrapped as json for the
    /// model.
    std::function<TResponseToOllama(const std::string &keyword,
                                    std::string /*complete_request_from_ollama*/)>
      resultProvider;
};

using TAiCommands = std::unordered_map<std::string, TAiCommand>;

/// @brief Returns global static list of all commands AI can use.
const TAiCommands &GetAiCommandsList();

class CAiLoopDetector
{
  public:
    static constexpr auto kMaxRepeats = 3;
    void Update(const std::string &command)
    {
        if (command == lastCommand)
        {
            ++counter;
        }
        else
        {
            lastCommand = command;
            counter = 1u;
        }
    }

    void Reset()
    {
        lastCommand = "";
        counter = 0u;
    }

    [[nodiscard]]
    bool IsLooping() const
    {
        return counter >= kMaxRepeats;
    }

  private:
    std::string lastCommand;
    std::size_t counter{0u};
};
