#include "http_socket.hpp" // IWYU pragma: keep

#include "socket.hpp" // IWYU pragma: keep

#include <array>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
constexpr std::string_view kEndOfHeaders = "\r\n\r\n";

} // namespace

CHttpSocket::CHttpSocket(CClientSocket aSocket) :
    iSocket(std::move(aSocket))
{
}

void CHttpSocket::ReadAll()
{
    std::string buffer;
    std::array<char, 4096> tmp{};
    std::vector<char> leftoverBody;
    leftoverBody.reserve(tmp.size());

    // Reading headers, it can take piece of the following body too. If so it will be in
    // leftoverBody.
    while (true)
    {
        auto [readStatus, readSize] = iSocket.read_all(tmp.data(), tmp.size());
        if (readStatus == EIoStatus::Error)
        {
            throw std::runtime_error("Error reading headers.");
        }
        buffer.append(tmp.data(), readSize);
        constexpr auto searchSize = kEndOfHeaders.size();
        const auto bufSize = buffer.size();
        if (bufSize >= searchSize)
        {
            const size_t pos = buffer.find(kEndOfHeaders, bufSize - readSize - searchSize);
            if (pos != std::string::npos)
            {
                const auto headersBodyBorder = pos + searchSize;
                iHeaders.Clear();
                iHeaders.ParseAndAdd(buffer.substr(0, headersBodyBorder));

                // Probably we read some more and got piece of the body.
                leftoverBody.assign(
                  std::next(buffer.begin(), static_cast<std::ptrdiff_t>(headersBodyBorder)),
                  buffer.end());
                break;
            }
        }
        if (readStatus == EIoStatus::OkReceivedZero)
        {
            // If we get here, we didn't read the \r\n\r\n yet, but connection was closed?
            throw std::runtime_error("Connection was closed without complete headers.");
        }
    }

    // TODO: Read the rest of the body.
}
