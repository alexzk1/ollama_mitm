#include "contentrestorator.hpp" // IWYU pragma: keep

#include <ollama/ollama.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

CContentRestorator::CContentRestorator(TAssistWords aWhatToLookFor) :
    whatToLookFor(std::move(aWhatToLookFor))
{
    // Sort whatToLookFor by string length
    std::sort(whatToLookFor.begin(), whatToLookFor.end(), [](const auto &a, const auto &b) {
        return a.size() < b.size();
    });
}

void CContentRestorator::Reset()
{
    lastData.clear();
    detectionHappened = false;
}

CContentRestorator::TDecision CContentRestorator::Update(const ollama::response &respFromOllama)
{
    if (detectionHappened)
    {
        return TAlreadyDetected{};
    }
    const auto &str = respFromOllama.as_simple_string();
    lastData.append(str);
    const std::size_t dataSize = lastData.size();

    // Find the upper bound for strings that could potentially match. These are those with size less
    // than dataSize. If the last string in whatToLookFor is larger than dataSize, we need more
    // data.
    const auto itEnd = std::upper_bound(whatToLookFor.begin(), whatToLookFor.end(), dataSize,
                                        [](std::size_t size, const std::string_view &str) {
                                            return size < str.size();
                                        });

    if (itEnd == whatToLookFor.end() && whatToLookFor.back().size() > dataSize)
    {
        return TNeedMoreData{};
    }

    // Check if any of the strings in whatToLookFor match the beginning of lastData. If so, we have
    // a detection.
    for (auto it = whatToLookFor.begin(); it != itEnd; ++it)
    {
        if (std::equal(it->begin(), it->end(), lastData.begin()))
        {
            detectionHappened = true;
            return TDetected{*it, std::move(lastData)};
        }
    }
    // If we reach here, it means no strings in whatToLookFor matched the beginning of lastData. We
    // need more data if there are still strings to check.
    if (itEnd != whatToLookFor.end())
    {
        return TNeedMoreData{};
    }
    // If we reach here, it means no strings in whatToLookFor matched the beginning of lastData and
    // there are no more strings to check. We pass the data to the user.
    detectionHappened = true;
    return TPassToUser{std::move(lastData)};
}
