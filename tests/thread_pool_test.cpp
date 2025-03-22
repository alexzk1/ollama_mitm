#include <common/threads_pool.hpp>

#include <atomic>
#include <chrono> // IWYU pragma: keep
#include <ctime>
#include <thread>

#include <gtest/gtest.h>

namespace Testing {

using namespace utility;
using namespace std::chrono_literals;

class ThreadPoolTest : public ::testing::Test
{
  public:
};

TEST_F(ThreadPoolTest, PoolWorks)
{
    std::atomic<int> counter{0};
    const int num_tasks = 10;

    auto task = [&counter](const auto &) {
        ++counter;
        std::this_thread::sleep_for(75ms); // NOLINT
    };

    {
        CThreadPool pool(4);
        for (int i = 0; i < num_tasks; ++i)
        {
            pool.enqueue(task);
        }
        std::this_thread::sleep_for(1200ms); // NOLINT
    }

    EXPECT_EQ(counter.load(), num_tasks);
}

TEST_F(ThreadPoolTest, PoolWithNoTasks)
{
    const CThreadPool pool(4);
    (void)pool;
}

TEST_F(ThreadPoolTest, PoolProperlyHandlesDestruction)
{
    std::atomic<int> counter{0};
    const int num_tasks = 10;
    const int pool_size = 4;

    auto task = [&counter](const auto &stopper) {
        ++counter;
        for (int i = 0; i < 1'000'000 && !(*stopper); ++i)
        {
            std::this_thread::sleep_for(75ms); // NOLINT
        }
    };
    const auto started_at = std::chrono::system_clock::now();
    {
        CThreadPool pool(pool_size);
        for (int i = 0; i < num_tasks; ++i)
        {
            pool.enqueue(task);
        }
        std::this_thread::sleep_for(1000ms); // NOLINT
    }
    const auto block_ended_at = std::chrono::system_clock::now();

    EXPECT_EQ(counter.load(), pool_size);
    EXPECT_LT(block_ended_at - started_at, 2000ms); // NOLINT
}

} // namespace Testing
