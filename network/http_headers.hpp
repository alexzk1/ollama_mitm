#pragma once

#include <common/cm_ctors.h>
#include <common/lambda_visitors.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace utility {
/// @brief Trims @p str from both ends, removes " \t\r\n" symbols.
inline void HttpTrim(std::string &str)
{
    static constexpr std::string_view kToRemove = " \t\r\n";
    const auto start = str.find_first_not_of(kToRemove);
    if (start != std::string::npos)
    {
        str = str.substr(start);
    }
    const auto end = str.find_last_not_of(kToRemove);
    if (end != std::string::npos)
    {
        const auto sizeToKeep = end + 1u;
        str = str.substr(0, sizeToKeep);
    }
}
} // namespace utility

/// @brief Represents 1st line of the HTTP request.
class HttpRequestLine
{
  public:
    HttpRequestLine() = default;

    /// @brief Construct object out of separated fields of request line.
    HttpRequestLine(std::string method, std::string path, std::string version) :
        iMethod(std::move(method)),
        iPath(std::move(path)),
        iVersion(std::move(version))
    {
        utility::HttpTrim(iMethod);
        utility::HttpTrim(iPath);
        utility::HttpTrim(iVersion);
    }
    ~HttpRequestLine() = default;
    DEFAULT_COPYMOVE(HttpRequestLine);

    /// @brief Constructs object out of a string formatted as "Method Path Version" (e.g., GET
    /// /index.html HTTP/1.1)
    explicit HttpRequestLine(const std::string &request_line)
    {
        std::istringstream stream(request_line);
        stream >> iMethod >> iPath >> iVersion;
        utility::HttpTrim(iMethod);
        utility::HttpTrim(iPath);
        utility::HttpTrim(iVersion);
    }

    /// @brief Converts HttpRequestLine to string
    /// @return std::string formatted as "Method Path Version" usable in http response.
    [[nodiscard]]
    std::string ToString() const
    {
        return iMethod + " " + iPath + " " + iVersion;
    }

    /// @returns true if all fields are set.
    [[nodiscard]]
    bool IsValid() const
    {
        return !iMethod.empty() && !iPath.empty() && !iVersion.empty();
    }

    bool operator==(const HttpRequestLine &other) const
    {
        const auto tie = [](const HttpRequestLine &what) {
            return std::tie(what.iMethod, what.iPath, what.iVersion);
        };
        return tie(*this) == tie(other);
    }

    /// @brief Getters
    [[nodiscard]]
    const std::string &GetMethod() const
    {
        return iMethod;
    }
    [[nodiscard]]
    const std::string &GetPath() const
    {
        return iPath;
    }
    [[nodiscard]]
    const std::string &GetVersion() const
    {
        return iVersion;
    }

  private:
    std::string iMethod;  // Method (GET, POST)
    std::string iPath;    // Path (/index.html)
    std::string iVersion; // Version(HTTP/1.1)
};

/// @brief Represents 1st line of the HTTP response.
class HttpResponseLine
{
  public:
    /// @brief Constructs object out of separated fields.
    explicit HttpResponseLine(std::string version, int status_code, std::string status_text) :
        iVersion(std::move(version)),
        iStatusCode(status_code),
        iStatusText(std::move(status_text))
    {
        utility::HttpTrim(iVersion);
        utility::HttpTrim(iStatusText);
    }

    ~HttpResponseLine() = default;
    DEFAULT_COPYMOVE(HttpResponseLine);

    /// @brief Constructs object out of a string formatted as "Version Status Code Status Text"
    explicit HttpResponseLine(const std::string &response_line)
    {
        std::istringstream stream(response_line);
        stream >> iVersion >> iStatusCode;
        // Status text may have spaces etc, getting everything till the end of the line.
        std::getline(stream, iStatusText, '\n');

        utility::HttpTrim(iVersion);
        utility::HttpTrim(iStatusText);
    }

    [[nodiscard]]
    std::string ToString() const
    {
        return iVersion + " " + std::to_string(iStatusCode) + " " + iStatusText;
    }

    /// @returns true if all fields are set.
    [[nodiscard]]
    bool IsValid() const
    {
        return !iVersion.empty() && IsValidStatusCode() && !iStatusText.empty();
    }

    [[nodiscard]]
    bool IsValidStatusCode() const
    {
        return iStatusCode >= 100 && iStatusCode <= 599;
    }

    bool operator==(const HttpResponseLine &other) const
    {
        const auto tie = [](const HttpResponseLine &what) {
            return std::tie(what.iStatusCode, what.iVersion, what.iStatusText);
        };
        return tie(*this) == tie(other);
    }

    ///@brief Getters
    [[nodiscard]]
    const std::string &GetVersion() const
    {
        return iVersion;
    }

    [[nodiscard]]
    int GetStatusCode() const
    {
        return iStatusCode;
    }

    [[nodiscard]]
    const std::string &GetStatusText() const
    {
        return iStatusText;
    }

  private:
    std::string iVersion;    // Version(HTTP/1.1)
    int iStatusCode{-1};     // Status code(200)
    std::string iStatusText; // Status text(OK)
};

/// @brief Represents header part of the http request or response with 1st line (everything before
/// the body).
class HttpHeaders
{
  public:
    using TFirstLine = std::variant<std::monostate, HttpRequestLine, HttpResponseLine>;

    HttpHeaders() = default;
    ~HttpHeaders() = default;
    DEFAULT_COPYMOVE(HttpHeaders);

    /// @brief Constructs the object and calls @fn ParseAndAdd(@p header_str).
    explicit HttpHeaders(const std::string &header_str)
    {
        ParseAndAdd(header_str);
    }

    /// @brief Parses "multiline" string as key/value pairs and stores it.
    /// Each line should be 1 key/value pair. First line is handled properly too.
    void ParseAndAdd(const std::string &header_str)
    {
        std::istringstream stream(header_str);
        std::string line;

        if (std::getline(stream, line, '\n'))
        {
            iFirstRequestLine = ParseFirstLine(line);
        }

        // Check if valid 1st line
        if (!std::visit(LambdaVisitor{[](const auto &first_line) {
                                          return first_line.IsValid();
                                      },
                                      [](std::monostate) {
                                          return false;
                                      }},
                        iFirstRequestLine))
        {
            throw std::invalid_argument("HTTP request / response does not look valid.");
        }

        while (std::getline(stream, line, '\n'))
        {
            if (line.empty() || line.find(':') == std::string::npos)
            {
                continue;
            }
            const auto colonPos = line.find(':');
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);

            utility::HttpTrim(key);
            utility::HttpTrim(value);
            iHeaders[key] = value;
        }
    }

    /// @returns the string usable to write to response made of internal state.
    std::string ToString() const
    {
        static constexpr std::string_view kEnd = "\r\n";
        std::ostringstream result;
        result << std::visit(LambdaVisitor{[](auto &&line) -> std::string {
                                               return line.ToString();
                                           },
                                           [](std::monostate) -> std::string {
                                               throw std::invalid_argument(
                                                 "Object is not initialized properly.");
                                               return {};
                                           }},
                             iFirstRequestLine)
               << kEnd;

        for (const auto &[key, value] : iHeaders)
        {
            result << key << ": " << value << kEnd;
        }
        result << kEnd;
        return result.str();
    }

    /// @returns value by key if found or empty string otherwise.
    /// @note You still can access the field as it is public.
    std::string Value(const std::string &key) const
    {
        const auto it = iHeaders.find(key);
        if (it != iHeaders.end())
        {
            return it->second;
        }
        return {};
    }

    /// @brief Makes object not initialized (empty).
    void Clear()
    {
        iFirstRequestLine = std::monostate{};
        iHeaders.clear();
    }

    static TFirstLine ParseFirstLine(const std::string &line)
    {
        static const std::vector<std::string_view> requestMarkers = {
          "GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS", "PATCH", "TRACE", "CONNECT"};

        std::istringstream stream(line);
        std::string firsrWord;
        stream >> firsrWord;
        utility::HttpTrim(firsrWord);

        if (firsrWord.find("HTTP/") == 0)
        {
            return HttpResponseLine(line);
        }

        const bool isRequest =
          std::any_of(requestMarkers.begin(), requestMarkers.end(), [&firsrWord](const auto &s) {
              return s == firsrWord;
          });
        if (isRequest)
        {
            return HttpRequestLine(line);
        }

        throw std::invalid_argument("Invalid HTTP first line.");
    }

    /// @returns true if this object was initialized from http request.
    bool IsRequest() const
    {
        const auto visitor = LambdaVisitor{
          [](const HttpRequestLine &) {
              return true;
          },
          [](const auto &) {
              return false;
          },
        };
        return std::visit(visitor, iFirstRequestLine);
    }

    /// @returns true if this object was initialized from http response.
    bool IsResponse() const
    {
        const auto visitor = LambdaVisitor{
          [](const HttpResponseLine &) {
              return true;
          },
          [](const auto &) {
              return false;
          },
        };
        return std::visit(visitor, iFirstRequestLine);
    }

    bool operator==(const HttpHeaders &other) const
    {
        const auto tie = [](const HttpHeaders &what) {
            return std::tie(what.iFirstRequestLine, what.iHeaders);
        };
        return tie(*this) == tie(other);
    }

  public:
    struct CaseInsensitiveHash
    {
        std::size_t operator()(const std::string &key) const
        {
            std::size_t hash = 0;
            for (const char c : key)
            {
                hash = hash * 31 + std::tolower(static_cast<unsigned char>(c));
            }
            return hash;
        }
    };

    struct CaseInsensitiveEqual
    {
        bool operator()(const std::string &a, const std::string &b) const
        {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char c1, char c2) {
                return std::tolower(static_cast<unsigned char>(c1))
                       == std::tolower(static_cast<unsigned char>(c2));
            });
        }
    };

    TFirstLine iFirstRequestLine;
    std::unordered_map<std::string, std::string, CaseInsensitiveHash, CaseInsensitiveEqual>
      iHeaders;
};
