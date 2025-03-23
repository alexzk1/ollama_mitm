#include <network/multi_accept_server.hpp>
#include <network/socket.hpp>

#include <algorithm>
#include <array>
#include <chrono> // IWYU pragma: keep
#include <cstdint>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace Testing {

using namespace std::chrono_literals;

class CMtServerTest : public ::testing::Test
{
  public:
    inline static constexpr std::uint16_t kServerPort = 33333;
    inline static const std::string kGet = "GET /\n";
    inline static const std::string kOk = "OK\n";
};

TEST_F(CMtServerTest, ServerWorks)
{
    CTcpServer server;

    const auto clientHandler = [](const auto & /*shouldStop*/, auto client_socket) {
        std::array<char, 1024u> tmp{0};
        const auto [readCode, readLen] =
          client_socket.read_all(tmp.data(), std::min(tmp.size(), kGet.size()));
        ASSERT_EQ(readCode, EIoStatus::Ok);
        ASSERT_EQ(readLen, kGet.size());

        const std::string response{tmp.begin(), tmp.begin() + readLen};
        EXPECT_TRUE(response.find(kGet) != std::string::npos);

        std::this_thread::sleep_for(25ms); // NOLINT
        const auto [writeCode, remainingToWriteLen] = client_socket.write(kOk);
        ASSERT_EQ(writeCode, EIoStatus::Ok);
        ASSERT_EQ(remainingToWriteLen, 0u);
        std::this_thread::sleep_for(50ms); // NOLINT
    };

    server.listen(kServerPort, clientHandler);

    // Client in the main thread.
    std::this_thread::sleep_for(250ms); // NOLINT
    CTcpClientConnecction client("localhost", kServerPort);
    for (int i = 0; i < 2; ++i)
    {
        client.connect();
        std::this_thread::sleep_for(50ms);
        const auto [write_code, remainingToWriteLen] = client.socket().write(kGet);
        ASSERT_EQ(write_code, EIoStatus::Ok);
        ASSERT_EQ(remainingToWriteLen, 0u);

        std::this_thread::sleep_for(77ms); // NOLINT
        std::array<char, 102'400u> tmp{0};
        const auto [readCode, readLen] =
          client.socket().read_all(tmp.data(), std::min(tmp.size(), kOk.size()));
        EXPECT_EQ(readCode, EIoStatus::Ok);
        ASSERT_EQ(readLen, kOk.size());
        EXPECT_EQ(std::string(tmp.begin(), tmp.begin() + readLen), kOk);
        client.disconnect();
        std::this_thread::sleep_for(333ms); // NOLINT
    }

    server.stop();
    EXPECT_EQ(client.connect(), EIoStatus::Error);
}

} // namespace Testing
