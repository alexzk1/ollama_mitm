#include <common/lambda_visitors.h>
#include <network/http_headers.hpp>
#include <network/http_starter.hpp>
#include <network/socket.hpp>

#include <cstddef>
#include <sstream>
#include <string>
#include <variant>

#include <gtest/gtest.h>

namespace Testing {

class CMockSocket
{
  public:
    inline static constexpr auto kHeadersCount = 4000;
    inline static constexpr auto kFirstLine = "HTTP/1.1 200 OK";

    CClientSocket::Result read_all(void *buf, std::size_t size_buf) const noexcept
    {
        auto read = input.readsome(static_cast<char *>(buf), size_buf);
        return {read > 0 ? EIoStatus::Ok : EIoStatus::OkReceivedZero, read};
    }

  private:
    std::string data{CreateProperMockHeaders()};
    mutable std::istringstream input{data};

    static std::string CreateProperMockHeaders()
    {
        std::stringstream ss;
        ss << kFirstLine << "\r\n";
        for (int i = 0; i < kHeadersCount; ++i)
        {
            ss << "Header-" << i << ": value " << i << "\r\n";
        }
        ss << "\r\n";
        for (int i = 0; i < kHeadersCount / 2; ++i)
        {
            ss << "BodyContent-" << i << " some value " << i << "\r\n";
        }
        return ss.str();
    }
};

class HttpStarterTest : public ::testing::Test
{
  public:
};

TEST_F(HttpStarterTest, StarterWorks)
{
    const CMockSocket mock_socket;
    const CHttpStarter starter(mock_socket, 0);

    EXPECT_EQ(starter.iHeaders.iHeaders.size(), CMockSocket::kHeadersCount);
    EXPECT_TRUE(starter.iHeaders.IsResponse());

    const LambdaVisitor visitor{
      [](const CHttpResponseLine &) {
      },
      [](const auto &) {
          ADD_FAILURE() << "Wrong parsed 1st line.";
      },
    };
    std::visit(visitor, starter.iHeaders.iFirstRequestLine);
}

} // namespace Testing
