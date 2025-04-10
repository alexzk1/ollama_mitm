#pragma once

#include "http_headers.hpp" // IWYU pragma: keep
#include "socket.hpp"       // IWYU pragma: keep

#include <common/cm_ctors.h>

#include <array>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/// @brief This class does initial reading on just established http connection,
/// it reads headers and may get some body's piece too which should be used later to make full body.
class CHttpStarter
{
  public:
    /// @brief End of headers marker.
    static inline constexpr std::string_view kEndOfHeaders = "\r\n\r\n";

    explicit CHttpStarter(const CClientSocket &aSocket) :
        CHttpStarter(aSocket, 0)
    {
    }

    template <typename taClientSocket>
    CHttpStarter(const taClientSocket &aSocket, int /*dummy*/)
    {
        std::string buffer;
        std::array<char, 4096> tmp{};
        // Reading headers, it can take piece of the following body too. If so it will be in
        // iBodyInitialPiece.
        while (true)
        {
            auto [readStatus, readSize] = aSocket.read_all(tmp.data(), tmp.size());
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
                    // We found the end of headers. Let's parse them and store piece of the body if
                    // any.
                    const auto headersBodyBorder = pos + searchSize;
                    iHeaders.Clear();
                    iHeaders.ParseAndAdd(buffer.substr(0, headersBodyBorder));

                    // Probably we read some more and got piece of the body.
                    iBodyInitialPiece.assign(
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
    }

    ~CHttpStarter() = default;
    DEFAULT_COPYMOVE(CHttpStarter);

    /// @brief Receives any body left in the socket in full.
    void ReceiveAllLeft()
    {
        std::array<char, 4096> tmp{};
        while (true)
        {
            auto [readStatus, readSize] = iSocket.read_all(tmp.data(), tmp.size());
            if (readStatus == EIoStatus::Error)
            {
                throw std::runtime_error("Error reading body.");
            }
            if (readStatus == EIoStatus::OkReceivedZero)
            {
                break;
            }
            iBodyInitialPiece.insert(iBodyInitialPiece.end(), tmp.begin(),
                                     std::next(tmp.begin(), readSize));
        }
    }

    /// @brief Writes current state of the object to the socket completely.
    void WriteTo(const CClientSocket &aSocket) const
    {
        aSocket.write_all(iHeaders.ToString().c_str(), iHeaders.ToString().size());
        aSocket.write_all(iBodyInitialPiece.data(), iBodyInitialPiece.size());
    }

  public:
    /// @brief Contains header and 1st line which can be request/response.
    CHttpHeaders iHeaders;

    /// @brief In the best case it should be empty, but may have some data, which should be
    /// accounted later on body reads.
    std::vector<char> iBodyInitialPiece;
};
