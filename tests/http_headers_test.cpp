#include <common/lambda_visitors.h>
#include <network/http_headers.hpp>

#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace Testing {
class HttpHeadersTest : public ::testing::Test
{
  public:
    static std::string trim(std::string str)
    {
        utility::HttpTrim(str);
        return str;
    }

    static bool IsRequest(const HttpHeaders::TFirstLine &firstLine)
    {
        const auto visitor = LambdaVisitor{
          [](const HttpRequestLine &) {
              return true;
          },
          [](const auto &) {
              return false;
          },
        };
        return std::visit(visitor, firstLine);
    }

    static bool IsResponse(const HttpHeaders::TFirstLine &firstLine)
    {
        const auto visitor = LambdaVisitor{
          [](const HttpResponseLine &) {
              return true;
          },
          [](const auto &) {
              return false;
          },
        };
        return std::visit(visitor, firstLine);
    }
};

TEST_F(HttpHeadersTest, HttpTrimWorks)
{
    using namespace utility;
    std::vector<std::string> test = {
      "   Hello World!   ",
      "\tHello World!  \r\r\n",
      "\n\r\t Hello World!\t \r \n",
    };
    const std::string expected = "Hello World!";
    for (auto &t : test)
    {
        HttpTrim(t);
        EXPECT_EQ(t, expected);
    }
}

TEST_F(HttpHeadersTest, HttpRequestLineIsParsed)
{
    using namespace utility;
    static const std::vector<std::string> test = {
      "GET / HTTP/1.1\r\n",
      "POST /api/v1/data HTTP/1.0\r\n",
      "PUT /update HTTP/1.1\r\n",
      "DELETE /delete HTTP/1.1\r\n",
    };
    for (const auto &t : test)
    {
        const HttpRequestLine line(t);
        auto prev = 0u;
        auto next = t.find(' ', prev);
        EXPECT_EQ(line.GetMethod(), trim(t.substr(prev, next - prev)));
        prev = next + 1;
        next = t.find(' ', prev);
        EXPECT_EQ(line.GetPath(), trim(t.substr(prev, next - prev)));
        prev = next + 1;
        next = t.find(' ', prev);
        EXPECT_EQ(line.GetVersion(), trim(t.substr(prev, next - prev)));
    }
}

TEST_F(HttpHeadersTest, HttpResponseLineParsed)
{
    using namespace utility;
    static const std::vector<std::string> test = {
      "HTTP/1.1 200 OK\r\n",
      "HTTP/1.0 404 Not Found\r\n",
      "HTTP/1.1 500 Internal Server Error\r\n",
    };
    for (const auto &t : test)
    {
        const HttpResponseLine line(t);
        auto prev = 0u;
        auto next = t.find(' ', prev);
        EXPECT_EQ(line.GetVersion(), trim(t.substr(prev, next - prev)));
        prev = next + 1;
        next = t.find(' ', prev);
        EXPECT_EQ(line.GetStatusCode(), std::stoi(trim(t.substr(prev, next - prev))));
        prev = next + 1;
        EXPECT_EQ(line.GetStatusText(), trim(t.substr(prev)));
    }
}

TEST_F(HttpHeadersTest, ParseFirstLineDetectsResponse)
{
    using namespace utility;
    static const std::vector<std::string> test = {
      "HTTP/1.1 200 OK  \r\n",    "GET / HTTP/1.1\r\n",          "POST /api/v1/data HTTP/1.0\r\n",
      "PUT /update HTTP/1.1\r\n", "DELETE /delete HTTP/1.1\r\n",
    };
    for (const auto &t : test)
    {
        auto firstLine = HttpHeaders::ParseFirstLine(t);
        EXPECT_EQ(IsResponse(firstLine), t.find("HTTP/") == 0);
    }
}

TEST_F(HttpHeadersTest, ParseFirstLineDetectsRequest)
{
    using namespace utility;
    static const std::vector<std::string> test = {
      "HTTP/1.1 200 OK \t\r\n",   "GET / HTTP/1.1\r\n",          "POST /api/v1/data HTTP/1.0\r\n",
      "PUT /update HTTP/1.1\r\n", "DELETE /delete HTTP/1.1\r\n",
    };
    for (const auto &t : test)
    {
        auto firstLine = HttpHeaders::ParseFirstLine(t);
        EXPECT_NE(IsRequest(firstLine), t.find("HTTP/") == 0);
    }
}

TEST_F(HttpHeadersTest, ParseFirstLineThrowsInvalidFirstLine)
{
    using namespace utility;
    static const std::vector<std::string> test = {
      "ABRVALG GET / HTTP/1.1\r\n",
      "WOOPS HTTP/1.1 200 OK\r\n",
    };
    for (const auto &t : test)
    {
        EXPECT_THROW(HttpHeaders::ParseFirstLine(t), std::invalid_argument);
    }
}

TEST_F(HttpHeadersTest, HeadersInRequestAreProperlyParsed)
{
    static const std::string headerString =
      "GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: "
      "curl/7.64.1\r\nInvalidHdrLine\r\t\r\nAccept: */*\r\n\r\nAndGotSomeBodyToo";
    const HttpHeaders headers(headerString);

    const auto checkFirstLine = LambdaVisitor{
      [](auto &&) {
          ASSERT_TRUE(false) << "Headers were parsed to the wrong type.";
      },
      [](const HttpRequestLine &firstLine) {
          EXPECT_EQ(firstLine.GetMethod(), "GET");
          EXPECT_EQ(firstLine.GetPath(), "/");
          EXPECT_EQ(firstLine.GetVersion(), "HTTP/1.1");
      },
    };
    std::visit(checkFirstLine, headers.iFirstRequestLine);

    EXPECT_EQ(headers.Value("Host"), "example.com");
    EXPECT_EQ(headers.Value("hoSt"), "example.com");
    EXPECT_EQ(headers.Value("User-Agent"), "curl/7.64.1");
    EXPECT_EQ(headers.Value("user-agEnt"), "curl/7.64.1");
    EXPECT_EQ(headers.Value("Accept"), "*/*");
    EXPECT_EQ(headers.Value("accept"), "*/*");
    EXPECT_TRUE(headers.Value("InvalidHdrLine").empty());

    const HttpHeaders headers2(headers.ToString());
    EXPECT_EQ(headers2, headers);
}

TEST_F(HttpHeadersTest, HeadersInResponseAreProperlyParsed)
{
    static const std::string headerString =
      "HTTP/1.1 200 OK And some long text\t  \r\nContent-Type: "
      "application/json\r\nInvalidHdrLine\nContent-Length: 42\r\n\r\nAndGotSomeBodyToo";
    const HttpHeaders headers(headerString);

    const auto checkFirstLine = LambdaVisitor{
      [](auto &&) {
          ASSERT_TRUE(false) << "Headers were parsed to the wrong type.";
      },
      [](const HttpResponseLine &firstLine) {
          EXPECT_EQ(firstLine.GetVersion(), "HTTP/1.1");
          EXPECT_EQ(firstLine.GetStatusCode(), 200);
          EXPECT_EQ(firstLine.GetStatusText(), "OK And some long text");
      },
    };
    std::visit(checkFirstLine, headers.iFirstRequestLine);

    EXPECT_EQ(headers.Value("Content-Type"), "application/json");
    EXPECT_EQ(headers.Value("content-type"), "application/json");
    EXPECT_EQ(headers.Value("Content-Length"), "42");
    EXPECT_EQ(headers.Value("content-length"), "42");
    EXPECT_TRUE(headers.Value("InvalidHdrLine").empty());

    const HttpHeaders headers2(headers.ToString());
    EXPECT_EQ(headers2, headers);
}

} // namespace Testing
