#include <common/runners.h>
#include <network/socket.hpp>

#include <algorithm>
#include <array>
#include <chrono> // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace Testing {

using namespace std::chrono_literals;

class CSocketsTest : public ::testing::Test
{
  public:
    inline static constexpr std::uint16_t kServerPort = 33333;
};

TEST_F(CSocketsTest, CopyCastTest)
{
    int value = 0x0142;
    auto ptr = copy_cast<char *>(&value);
    // NOLINTNEXTLINE
    ASSERT_EQ(reinterpret_cast<std::size_t>(ptr), reinterpret_cast<std::size_t>(&value));
}

TEST_F(CSocketsTest, NameResolverTest)
{
    const auto ips = CClientSocket::hostname_to_ip("localhost", EIpType::IPv4);
    EXPECT_FALSE(ips.empty());
    EXPECT_TRUE(std::any_of(ips.begin(), ips.end(), [](const std::string &ip) {
        return ip == "127.0.0.1";
    }));

    const auto google = CClientSocket::hostname_to_ip("google.com", EIpType::IPv4);
    EXPECT_FALSE(google.empty());
    EXPECT_TRUE(std::none_of(google.begin(), google.end(), [](const std::string &ip) {
        return ip == "127.0.0.1";
    }));

    const auto nums = CClientSocket::hostname_to_ip("127.0.0.1", EIpType::IPv4);
    EXPECT_EQ(nums.size(), 1u);
    EXPECT_TRUE(std::any_of(nums.begin(), nums.end(), [](const std::string &ip) {
        return ip == "127.0.0.1";
    }));

    const auto nums2 = CClientSocket::hostname_to_ip("1.1.1.1", EIpType::IPv4);
    EXPECT_EQ(nums2.size(), 1u);
    EXPECT_TRUE(std::any_of(nums2.begin(), nums2.end(), [](const std::string &ip) {
        return ip == "1.1.1.1";
    }));
}

TEST_F(CSocketsTest, ClientConnectWriteRead)
{
    CTcpClientConnecction client;
    const auto status = client.connect("google.com", 80);
    EXPECT_NE(status, EIoStatus::Error);

    const auto [write_code, wrtie_len] = client.socket().write("GET /\n");
    ASSERT_NE(write_code, EIoStatus::Error);

    std::array<char, 102'400u> tmp{0};
    const auto [readCode, readLen] = client.socket().read_all(tmp.data(), tmp.size());
    EXPECT_NE(readCode, EIoStatus::Error);
    ASSERT_GT(readLen, 0);

    const std::string response{tmp.begin(), tmp.begin() + readLen};
    EXPECT_TRUE(response.find("HTTP/1.") != std::string::npos);
}

TEST_F(CSocketsTest, ServerAcceptsAndCommunicatesWithClient)
{
    static const std::string kGet = "GET /\n";
    static const std::string kOk = "OK\n";

    // Server thread.
    auto serverThread = utility::startNewRunner([](const auto &interrupt_ptr) {
        CTcpAcceptServer server{kServerPort};
        std::array<char, 1024u> tmp{0};
        while (!(*interrupt_ptr))
        {
            const auto client_socket = server.accept_autoclose(interrupt_ptr);
            if (!client_socket)
            {
                continue;
            }
            const auto [readCode, readLen] =
              client_socket.read_all(tmp.data(), std::min(tmp.size(), kGet.size()));
            ASSERT_NE(readCode, EIoStatus::Error);
            ASSERT_EQ(readLen, kGet.size());

            const std::string response{tmp.begin(), tmp.begin() + readLen};
            EXPECT_TRUE(response.find(kGet) != std::string::npos);

            std::this_thread::sleep_for(25ms); // NOLINT
            const auto [writeCode, remainingToWriteLen] = client_socket.write(kOk);
            ASSERT_NE(writeCode, EIoStatus::Error);
            ASSERT_EQ(remainingToWriteLen, 0u);
            std::this_thread::sleep_for(50ms); // NOLINT
        }
    });

    // Client in the main thread.
    std::this_thread::sleep_for(250ms);
    CTcpClientConnecction client("localhost", kServerPort);
    for (int i = 0; i < 2; ++i)
    {
        client.connect();
        std::this_thread::sleep_for(50ms); // NOLINT
        const auto [write_code, remainingToWriteLen] = client.socket().write(kGet);
        ASSERT_EQ(write_code, EIoStatus::Ok);
        ASSERT_EQ(remainingToWriteLen, 0u);

        std::this_thread::sleep_for(77ms); // NOLINT
        std::array<char, 102'400u> tmp{0};
        const auto [readCode, readLen] =
          client.socket().read_all(tmp.data(), std::min(tmp.size(), kOk.size()));
        EXPECT_NE(readCode, EIoStatus::Error);
        ASSERT_EQ(readLen, kOk.size());
        EXPECT_EQ(std::string(tmp.begin(), tmp.begin() + readLen), kOk);
        client.disconnect();
        std::this_thread::sleep_for(333ms); // NOLINT
    }
    serverThread.reset();

    EXPECT_EQ(client.connect(), EIoStatus::Error);
}

}; // namespace Testing
