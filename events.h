#pragma once
#include <thread>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

class EventBusClient {
public:
    using OnEventCallback = std::function<void(const std::string&)>;

    bool start(uint16_t busPort);
    void stop();
    void setOnEvent(OnEventCallback cb) { onEvent_ = cb; }

private:
    int sock_ = -1;
    std::thread thr_;
    std::atomic<bool> running_{false};
    OnEventCallback onEvent_;
};
