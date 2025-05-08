#pragma once

#include <common/cm_ctors.h>
#include <date/date.h>
#include <date/tz.h>
#include <ollama/json.hpp>
#include <ollama/ollama.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <utility>

/// @brief Generates ping response to user.
class CUserPingGenerator
{
  public:
    NO_COPYMOVE(CUserPingGenerator);
    CUserPingGenerator() = delete;
    ~CUserPingGenerator() = default;

    explicit CUserPingGenerator(std::string model) :
        model(std::move(model))
    {
    }

    /// @brief Generates a JSON response with a ping message.
    /// @returns ping message properly wrapped in json.
    std::string GeneratePingResponse() const
    {
        std::string text = ".";
        std::call_once(once, [&text]() {
            text = "Working.";
        });
        return BuildJsString(std::move(text));
    }

    ollama::response GeneratePingResponse(const ollama::response &response) const
    {

        std::string text = ".";
        std::call_once(once, [&text]() {
            text = "Working.";
        });
        return ReplaceOllamaText(response, std::move(text));

        return {};
    }

    /// @brief If it was pings before @returns finishing response, and empty string otherwise.
    /// @returns finishing message properly wrapped in json IF it was GeneratePingResponse() before
    /// or empty string otherwise.
    std::string FinishPingsIfAny() const
    {
        bool emptyResullt = false;
        std::call_once(once, [&]() {
            emptyResullt = true;
        });

        if (emptyResullt)
        {
            return {};
        }
        return BuildJsString(kFinishMessage);
    }

    ollama::response FinishPingsIfAny(const ollama::response &response) const
    {
        bool emptyResullt = false;
        std::call_once(once, [&]() {
            emptyResullt = true;
        });

        if (emptyResullt)
        {
            return {};
        }
        return ReplaceOllamaText(response, kFinishMessage);
    }

    [[nodiscard]]
    static std::string BuildJsStringForUser(const std::string &model, std::string text)
    {
        // Example of allama answer:
        //{"created_at":"2025-04-26T12:13:59.246926495Z","done":false,
        //"message":{"content":"0","role":"assistant"},"model":"qwen2.5-coder:7b"}

        nlohmann::json cont;
        cont["content"] = std::move(text);
        cont["role"] = "assistant";

        nlohmann::json js;
        js["created_at"] = GetUtcTime();
        js["done"] = false;
        js["model"] = model;
        js["message"] = std::move(cont);

        return js.dump();
    }

    [[nodiscard]]
    std::string BuildJsString(std::string text) const
    {
        return BuildJsStringForUser(model, std::move(text));
    }

    /// @brief Replaces the content of an ollama response with a message.
    /// @param response - The original ollama response.
    /// @param text - The plain text message (not json) to replace the content with.
    /// @returns The modified ollama response.
    [[nodiscard]]
    static ollama::response ReplaceOllamaText(const ollama::response &response, std::string text)
    {
        auto jobj = response.as_json();
        jobj["done"] = false;
        jobj["message"]["content"] = std::move(text);
        return ollama::response(jobj.dump(), ollama::message_type::chat);
    }

    [[nodiscard]]
    static std::string GetUtcTime()
    {
        auto now = date::floor<std::chrono::microseconds>(date::utc_clock::now());
        return date::format("%FT%TZ", now);
    }

    // Just \n is filtered out.
    constexpr inline static auto kFinishMessage = ".\n\n";

  private:
    mutable std::once_flag once;
    std::string model;
};
