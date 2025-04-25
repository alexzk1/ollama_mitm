#pragma once

#include <commands/ollama_commands.hpp>

#include <cctype>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <string>

enum class EOllamaProxyVerbosity : std::uint8_t {
    Silent = 0,
    Error = 0x10,
    Warning = 0x20,
    Debug = 0xFF,
};

struct TOllamaProxyConfig
{
    EOllamaProxyVerbosity verbosity{EOllamaProxyVerbosity::Silent};
    std::string ollamaHost{"localhost"};
    int ollamaPort{11434};
    std::ostream &outStream{std::cout};
    std::ostream &errorStream{std::cerr};

    /// @brief Checks if the verbosity level is fitting.
    [[nodiscard]]
    bool IsFittingVerbosity(const EOllamaProxyVerbosity value) const
    {
        return static_cast<std::uint8_t>(verbosity) >= static_cast<std::uint8_t>(value);
    }

    /// @brief Executes the given function if the verbosity level is fitting. Usable for logging.
    /// Passes the output stream to the function.
    /// @param value The verbosity level to check against. If it's higher or equal, the function
    /// will be executed.
    /// @param func The function to execute if the verbosity level is fitting. It should take an
    /// std::ostream& as parameter.
    template <typename taFunc>
    void ExecIfFittingVerbosity(const EOllamaProxyVerbosity value, const taFunc &func) const
    {
        if (IsFittingVerbosity(value))
        {
            func(value == EOllamaProxyVerbosity::Error ? errorStream : outStream);
        }
    }

    /// @brief Checks if the configuration is valid.
    [[nodiscard]]
    bool Validate() const
    {
        bool res = !ollamaHost.empty() && ollamaPort > 0 && ollamaPort <= 65535;
        for (const char ch : ollamaHost)
        {
            if (!res)
            {
                break;
            }
            if (ch == '-' || ch == '.' || std::isalnum(ch))
            {
                continue;
            }
            res = false;
        }
        return res;
    }

    /// @returns A string representing the URL to connect to Ollama.
    [[nodiscard]]
    std::string CreateOllamaUrl() const
    {
        return "http://" + ollamaHost + ":" + std::to_string(ollamaPort);
    }

    /// @returns A reference to the list of AI commands.
    [[nodiscard]]
    const TAiCommands &GetAiCommands() const
    {
        return GetAiCommandsList();
    }
};
