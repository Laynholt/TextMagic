#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

constexpr std::intptr_t kCompletionHandled = 1;

constexpr bool CompletionOwnershipTransferred(
    bool delivered,
    std::uintptr_t result
) {
    return delivered && result == static_cast<std::uintptr_t>(kCompletionHandled);
}

class CompletionRegistry {
public:
    template <typename T>
    std::uintptr_t Store(std::unique_ptr<T> payload) {
        static_assert(!std::is_array_v<T>, "completion payloads must be objects");
        if (!payload) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_enabled) {
            return 0;
        }

        std::uintptr_t token = m_nextToken++;
        if (token == 0) {
            token = m_nextToken++;
        }
        m_payloads.emplace(token, std::make_unique<Holder<T>>(std::move(payload)));
        return token;
    }

    template <typename T>
    std::unique_ptr<T> Take(std::uintptr_t token) {
        if (token == 0) {
            return {};
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_payloads.find(token);
        if (found == m_payloads.end()) {
            return {};
        }
        auto* holder = dynamic_cast<Holder<T>*>(found->second.get());
        if (!holder) {
            return {};
        }
        std::unique_ptr<T> payload = std::move(holder->payload);
        m_payloads.erase(found);
        return payload;
    }

    bool Remove(std::uintptr_t token) {
        if (token == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_payloads.erase(token) != 0;
    }

    void DisableAndClear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_enabled = false;
        m_payloads.clear();
    }

private:
    struct HolderBase {
        virtual ~HolderBase() = default;
    };

    template <typename T>
    struct Holder final : HolderBase {
        explicit Holder(std::unique_ptr<T> value) : payload(std::move(value)) {}
        std::unique_ptr<T> payload;
    };

    std::mutex m_mutex;
    std::map<std::uintptr_t, std::unique_ptr<HolderBase>> m_payloads;
    std::uintptr_t m_nextToken = 1;
    bool m_enabled = true;
};
