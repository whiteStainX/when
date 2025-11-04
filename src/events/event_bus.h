#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace why {
namespace events {

class EventBus {
public:
    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    template<typename EventT>
    using Handler = std::function<void(const EventT&)>;

    class SubscriptionHandle {
    public:
        SubscriptionHandle() = default;
        SubscriptionHandle(const SubscriptionHandle&) = delete;
        SubscriptionHandle& operator=(const SubscriptionHandle&) = delete;
        SubscriptionHandle(SubscriptionHandle&& other) noexcept
            : bus_(other.bus_), type_(other.type_), id_(other.id_) {
            other.bus_ = nullptr;
        }
        SubscriptionHandle& operator=(SubscriptionHandle&& other) noexcept {
            if (this != &other) {
                reset();
                bus_ = other.bus_;
                type_ = other.type_;
                id_ = other.id_;
                other.bus_ = nullptr;
            }
            return *this;
        }
        ~SubscriptionHandle() { reset(); }

        void reset() {
            if (bus_) {
                bus_->unsubscribe(type_, id_);
                bus_ = nullptr;
                type_ = std::type_index(typeid(void));
                id_ = 0;
            }
        }

        explicit operator bool() const { return bus_ != nullptr; }

    private:
        friend class EventBus;
        SubscriptionHandle(EventBus* bus, std::type_index type, std::size_t id)
            : bus_(bus), type_(type), id_(id) {}

        EventBus* bus_ = nullptr;
        std::type_index type_{typeid(void)};
        std::size_t id_ = 0;
    };

    template<typename EventT>
    SubscriptionHandle subscribe(Handler<EventT> handler) {
        auto wrapper = [handler = std::move(handler)](const void* event_ptr) {
            handler(*static_cast<const EventT*>(event_ptr));
        };
        const std::type_index key(typeid(EventT));
        auto& bucket = subscribers_[key];
        const std::size_t id = next_id_++;
        bucket.push_back(SubscriberEntry{id, true, std::move(wrapper)});
        return SubscriptionHandle(this, key, id);
    }

    template<typename EventT>
    void publish(const EventT& event) const {
        auto it = subscribers_.find(std::type_index(typeid(EventT)));
        if (it == subscribers_.end()) {
            return;
        }
        for (const auto& entry : it->second) {
            if (entry.active) {
                entry.handler(&event);
            }
        }
    }

    void reset() {
        subscribers_.clear();
        next_id_ = 0;
    }

private:
    using HandlerWrapper = std::function<void(const void*)>;

    struct SubscriberEntry {
        std::size_t id;
        bool active;
        HandlerWrapper handler;
    };

    void unsubscribe(const std::type_index& type, std::size_t id) {
        auto it = subscribers_.find(type);
        if (it == subscribers_.end()) {
            return;
        }
        auto& entries = it->second;
        for (auto& entry : entries) {
            if (entry.id == id) {
                entry.active = false;
                break;
            }
        }
        entries.erase(std::remove_if(entries.begin(), entries.end(), [](const SubscriberEntry& entry) {
                           return !entry.active;
                       }),
                       entries.end());
        if (entries.empty()) {
            subscribers_.erase(it);
        }
    }

    std::unordered_map<std::type_index, std::vector<SubscriberEntry>> subscribers_;
    std::size_t next_id_ = 0;
};

} // namespace events
} // namespace why

