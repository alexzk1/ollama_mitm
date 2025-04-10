#include "http_socket.hpp" // IWYU pragma: keep

#include "network/http_headers.hpp"
#include "socket.hpp" // IWYU pragma: keep

#include <algorithm>
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
const CHttpHeaders::CaseInsensitiveEqual kInsensitiveStringEq{};

// FIXME: revise this AI generated.
/*
class ChunkedReader
{
  public:
    explicit ChunkedReader(const CClientSocket &socket, std::string leftover) :
        iSocket(socket),
        iBuffer(std::move(leftover))
    {
    }

    /// @brief Читает и возвращает следующий чанк, либо пустую строку, если всё кончено.
    std::string ReadNextChunk()
    {
        if (iFinished)
            return {}; // Все чанки уже прочитаны

        while (true)
        {
            if (iChunkSize == 0)
            {
                if (!ReadChunkSize()) // Конец передачи (0-чанк)
                {
                    iFinished = true;
                    return {};
                }
            }

            std::string chunk = ReadChunkData();
            if (!chunk.empty())
                return chunk;
        }
    }

    /// @brief Читает все чанки в строку.
    std::string ReadAllChunks()
    {
        std::string result;
        while (!iFinished)
            result += ReadNextChunk();
        return result;
    }

  private:
    const CClientSocket &iSocket;
    std::string iBuffer;
    size_t iChunkSize = 0;
    bool iFinished = false;

    /// @brief Читает размер чанка. Возвращает `false`, если это был 0-чанк (конец передачи).
    bool ReadChunkSize()
    {
        std::string line = ReadLine();
        iChunkSize = std::stoul(line, nullptr, 16); // Hex -> число
        return iChunkSize != 0;
    }

    /// @brief Читает `iChunkSize` байт данных + `\r\n` в конце чанка.
    std::string ReadChunkData()
    {
        if (iBuffer.size() < iChunkSize + 2)
            FillBuffer(iChunkSize + 2);

        if (iBuffer.size() >= iChunkSize + 2)
        {
            std::string chunk = iBuffer.substr(0, iChunkSize);
            iBuffer.erase(0, iChunkSize + 2); // +2 для `\r\n`
            iChunkSize = 0;
            return chunk;
        }
        return {}; // Не должно случаться, просто защита
    }

    /// @brief Читает строку (до `\r\n`).
    std::string ReadLine()
    {
        while (true)
        {
            size_t pos = iBuffer.find("\r\n");
            if (pos != std::string::npos)
            {
                std::string line = iBuffer.substr(0, pos);
                iBuffer.erase(0, pos + 2);
                return line;
            }

            FillBuffer(128);
        }
    }

    /// @brief Дочитывает из сокета, если данных не хватает.
    void FillBuffer(size_t minSize)
    {
        while (iBuffer.size() < minSize)
        {
            char tmp[1024];
            auto [status, bytesRead] = iSocket.read_all(tmp, sizeof(tmp));
            if (status == EIoStatus::Error)
                throw std::runtime_error("Socket read error");

            iBuffer.append(tmp, bytesRead);
            if (bytesRead == 0)
                break; // Подключение закрыто
        }
    }
};
*/

void ReadFixedSizeBody(const CClientSocket &iSocket, const size_t contentLength,
                       std::vector<char> &fullBody)
{
    fullBody.reserve(contentLength);
    size_t totalRead = fullBody.size();

    std::array<char, 4096> buffer{};
    while (totalRead < contentLength)
    {
        const size_t toRead = std::min(buffer.size(), contentLength - totalRead);
        auto [status, bytesRead] = iSocket.read_all(buffer.data(), toRead);

        if (status == EIoStatus::Error)
        {
            throw std::runtime_error("Error reading body");
        }

        if (status == EIoStatus::OkReceivedZero)
        {
            throw std::runtime_error("Connection closed before body was fully read");
        }

        fullBody.insert(fullBody.end(), buffer.begin(), buffer.begin() + bytesRead);
        totalRead += bytesRead;
    }
}

void ReadUntilEOF(const CClientSocket &iSocket, std::vector<char> &fullBody)
{
    std::vector<char> buffer(4096);

    while (true)
    {
        auto [status, bytesRead] = iSocket.read_all(buffer.data(), buffer.size());

        if (status == EIoStatus::Error)
        {
            throw std::runtime_error("Error reading body from the socket.");
        }

        if (bytesRead > 0)
        {
            fullBody.insert(fullBody.end(), buffer.begin(),
                            buffer.begin() + static_cast<std::ptrdiff_t>(bytesRead));
        }

        if (status == EIoStatus::OkReceivedZero)
        {
            break; // EOF
        }
    }
}

void ReadHttpBody(const CClientSocket &iSocket, const CHttpHeaders &iHeaders,
                  std::vector<char> &leftoverBody, std::vector<char> &fullBody)
{
    fullBody.clear();

    // 1. If we had some lefover after reading the headers, add it to body.
    if (!leftoverBody.empty())
    {
        fullBody.insert(fullBody.end(), leftoverBody.begin(), leftoverBody.end());
        leftoverBody.clear();
    }

    // 2. Check Content-Length
    auto it = iHeaders.iHeaders.find("Content-Length");
    if (it != iHeaders.iHeaders.end())
    {
        const std::size_t contentLength = std::stoul(it->second);
        ReadFixedSizeBody(iSocket, contentLength, fullBody);
        return;
    }

    // 3. Check Transfer-Encoding: chunked
    if (kInsensitiveStringEq(iHeaders.Value("Transfer-Encoding"), "chunked"))
    {
        // ReadChunkedBody(iSocket, fullBody);
        throw std::runtime_error("Not implemented");
        return;
    }

    // 4. If both Content-Length and Transfer-Encoding are missing, read till connection closed.
    ReadUntilEOF(iSocket, fullBody);
}

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
