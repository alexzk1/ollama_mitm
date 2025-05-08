#include "ollama_commands.hpp" // IWYU pragma: keep

#include <date/date.h>
#include <date/tz.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>
#include <string>
#include <utility>

namespace {
// trim from start (in place)
void ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
}

// trim from end (in place)
void rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char ch) {
                             return !std::isspace(ch);
                         })
              .base(),
            s.end());
}

void trim(std::string &s)
{
    rtrim(s);
    ltrim(s);
}

std::string trim_copy(std::string s)
{
    trim(s);
    return s;
}

std::string ProvideDateTimeForAi(const std::string &)
{
    using namespace date;
    using namespace std::chrono;
    auto tm = system_clock::now();
    auto now = zoned_time{current_zone(), tm};
    auto info = current_zone()->get_info(tm);
    std::ostringstream oss;
    oss << date::format("%A %FT%T%Ez", now);
    oss << "\nDST is ";
    oss << (info.save != std::chrono::seconds{0} ? "active" : "disabled") << " now.";
    return oss.str();
}

/// @brief use this one to make typical heuristic when keyword does not assume more data passed.
template <typename taCallable>
TResponseToOllama MakeTypicalResponseForKeywordOnly(const taCallable &answerProvider,
                                                    const std::string &keyword, std::string request)
{
    const auto r = trim_copy(request);
    if (r == keyword)
    {
        return ThatWasRequestToFulfill{answerProvider(request)};
    }
    return TThatWasResponseToUser{std::move(request)};
}

} // namespace

// Warning! Callbacks must return plain response, not wrapped to json. Just information to pass.
// You cab use ${KEYWORD} which will be replaced by actual one prior sending to AI.
const TAiCommands &GetAiCommandsList()
{
    static const TAiCommands list = {
      {"AI_DATE_TIME_NOW",
       {"You have access to real current local date and time value now. To check it respond with "
        "single word ${KEYWORD}.\nYou will receive reply with current local system date and time "
        "in ISO 8601 format including time zone offset (e.g., Monday "
        "2025-04-25T16:10:00+03:00).\nTreat "
        "received value as fact, as current known date and time.\nTranslate the fact to proper "
        "language user uses.",
        [](const std::string &keyword, std::string request) {
            return MakeTypicalResponseForKeywordOnly(ProvideDateTimeForAi, keyword,
                                                     std::move(request));
        }}},
    };

    return list;
}
