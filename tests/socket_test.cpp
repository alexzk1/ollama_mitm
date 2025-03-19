#include "socket.hpp" // IWYU pragma: keep

#include <algorithm>
#include <cstddef>
#include <string>

#include <gtest/gtest.h>

namespace Testing {
class CSocketsTest : public ::testing::Test
{
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
    const auto ips = client_socket_t::hostname_to_ip("localhost", EIpType::IPv4);
    EXPECT_FALSE(ips.empty());
    EXPECT_TRUE(std::any_of(ips.begin(), ips.end(), [](const std::string &ip) {
        return ip == "127.0.0.1";
    }));

    const auto google = client_socket_t::hostname_to_ip("google.com", EIpType::IPv4);
    EXPECT_FALSE(google.empty());
    EXPECT_TRUE(std::none_of(google.begin(), google.end(), [](const std::string &ip) {
        return ip == "127.0.0.1";
    }));

    const auto nums = client_socket_t::hostname_to_ip("127.0.0.1", EIpType::IPv4);
    EXPECT_EQ(nums.size(), 1u);
    EXPECT_TRUE(std::any_of(nums.begin(), nums.end(), [](const std::string &ip) {
        return ip == "127.0.0.1";
    }));

    const auto nums2 = client_socket_t::hostname_to_ip("1.1.1.1", EIpType::IPv4);
    EXPECT_EQ(nums2.size(), 1u);
    EXPECT_TRUE(std::any_of(nums2.begin(), nums2.end(), [](const std::string &ip) {
        return ip == "1.1.1.1";
    }));
}

}; // namespace Testing
