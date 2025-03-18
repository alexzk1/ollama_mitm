#pragma once

#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

namespace utility {
using runnerint_t = std::shared_ptr<std::atomic<bool>>;
using runner_f_t = std::function<void(const runnerint_t should_int)>;

// simple way to execute lambda in thread, in case when shared_ptr is cleared it will send
// stop notify and join(), so I can ensure 1 pointer has only 1 running thread always for the same
// task.
inline auto startNewRunner(runner_f_t func)
{
    using res_t = std::shared_ptr<std::thread>;
    auto stop = runnerint_t(new std::atomic<bool>(false));
    return res_t(new std::thread(func, stop), [stop](auto ptrToDelete) {
        stop->store(true);
        if (ptrToDelete)
        {
            if (ptrToDelete->joinable())
            {
                ptrToDelete->join();
            }
            delete ptrToDelete;
        }
    });
}

inline size_t currentThreadId()
{
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}
} // namespace utility
