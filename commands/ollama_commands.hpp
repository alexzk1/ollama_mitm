#pragma once

#include <functional>
#include <string>
#include <vector>

/// @brief Describes single command from AI to backend.
struct TAiCommand
{
    /// @brief Keyword AI must use.
    std::string keyword;
    /// @brief Instructions to pass to the AI.
    std::string instructionForAi;
    /// @brief Functor which does actual job. Returns plain result, NOT wrapped as json for the
    /// model.
    std::function<std::string(const std::string & /*complete_request_from_ollama*/)> resultProvider;
};

using TAiCommands = std::vector<TAiCommand>;

/// @brief Returns global static list of all commands AI can use.
const TAiCommands &GetAiCommandsList();
