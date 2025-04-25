#include "ollama_commands.hpp" // IWYU pragma: keep

#include <date/date.h>
#include <date/tz.h>

#include <chrono>
#include <string>

namespace {
std::string ProvideDateTimeForAi(const std::string &)
{
    // TODO: add date time saving info to answer.
    using namespace date;
    using namespace std::chrono;
    auto now = zoned_time{current_zone(), system_clock::now()};
    return format("%FT%T%Ez", now);
}
} // namespace

// Warning! Callbacks must return plain response, not wrapped to json. Just information to pass.
const TAiCommands &GetAiCommandsList()
{
    static const TAiCommands list = {
      TAiCommand{"AI_DATE_TIME_NOW",
                 "You may request the current local date and time by responding with the keyword "
                 "AI_DATE_TIME_NOW.\nWhen you do so, the system will reply with the timestamp in "
                 "ISO 8601 format including time zone offset (e.g., 2025-04-25T16:10:00+03:00).\n"
                 "After receiving this, you are expected to continue your reasoning or answer "
                 "accordingly.\nThe timestamp should be treated as a fact.",
                 ProvideDateTimeForAi},
    };

    return list;
}
