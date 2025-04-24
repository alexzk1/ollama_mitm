#pragma once

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

template <typename taStoredType, typename taMutex = std::mutex,
          typename taQueueType = std::queue<taStoredType>>
class SafeQueue
{
  public:
    using size_type = typename taQueueType::size_type;
    using value_type = taStoredType;
    using mutex_type = taMutex;
    using queue_type = taQueueType;

    /// @brief Pushes new element to the underlaying queue.
    void push(taStoredType item)
    {
        std::lock_guard<taMutex> lock(mutex);
        dataQueue.push(std::move(item));
        conditional_queue.notify_one();
    }

    /// @brief Constructs new element with provided args and pushes it to queue.
    template <class... Args>
    void emplace(Args &&...args)
    {
        push(taStoredType(std::forward<Args>(args)...));
    }

    /// @brief Pops element from the queue without waiting.
    /// @returns std::nullopt if queue is empty, value otherwise.
    [[nodiscard]]
    std::optional<taStoredType> pop()
    {
        std::lock_guard<taMutex> lock(mutex);
        if (dataQueue.empty())
        {
            return std::nullopt;
        }
        auto item = std::move(dataQueue.front());
        dataQueue.pop();
        return item;
    }

    /// @brief Clears the queue.
    void clear()
    {
        {
            taQueueType tmp;
            std::lock_guard<taMutex> lock(mutex);
            std::swap(dataQueue, tmp);
        }
    }

    /// @brief Checks if queue is empty.
    /// @returns true if queue is empty, false otherwise.
    [[nodiscard]]
    bool empty()
    {
        std::lock_guard<taMutex> lock(mutex);
        return dataQueue.empty();
    }

    /// @brief Returns the number of elements in the queue.
    /// @returns Number of elements in the queue.
    [[nodiscard]]
    size_type size()
    {
        std::lock_guard<taMutex> lock(mutex);
        return dataQueue.size();
    }

  private:
    taQueueType dataQueue;
    taMutex mutex;
    std::condition_variable conditional_queue;
};
