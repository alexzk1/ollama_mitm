#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <type_traits>

namespace utility {
using runnerint_t = std::shared_ptr<std::atomic<bool>>;
using runner_f_t = std::function<void(const runnerint_t should_int)>;

/// @brief Simple way to execute lambda in thread, in case when shared_ptr is cleared it will send
/// stop notify and join(), so I can ensure 1 pointer has only 1 running thread always for the same
/// task.
/// @param func - callable with 1 parameter which accepts runnerint_t. If stored value into atomic
/// is true, callable should exit.
/// @returns std::shared_ptr<std::thread> of the new thread. This pointer will .join() thread on
/// destruction and signal callable to stop.
template <typename taCallable>
std::shared_ptr<std::thread> startNewRunner(taCallable &&func)
{
    static_assert(std::is_assignable_v<runner_f_t, taCallable>,
                  "Callable should accepts runnerint_t as parameter");
    static_assert(std::is_invocable_v<taCallable, runnerint_t>,
                  "Callable should be invocable with runnerint_t as parameter");

    using res_t = std::shared_ptr<std::thread>;
    auto stop = std::make_shared<std::atomic<bool>>(false);
    return res_t(new std::thread(std::forward<taCallable>(func), stop), [stop](auto ptrToDelete) {
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

///@returns some ID of the current thread. It is system dependant.
inline std::size_t currentThreadId()
{
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}
} // namespace utility
