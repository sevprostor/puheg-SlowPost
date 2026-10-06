#include "events.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>

bool EventBusClient::start(uint16_t busPort) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) < 0) return false;

    sockaddr_in bus{};
    bus.sin_family = AF_INET;
    bus.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bus.sin_port = htons(busPort);
    if (sendto(sock_, "subscribe", 9, 0, (sockaddr*)&bus, sizeof(bus)) < 0)
        return false;

    running_ = true;
    thr_ = std::thread([this] {
        char buf[2048];
        while (running_) {
            fd_set rfds; FD_ZERO(&rfds); FD_SET(sock_, &rfds);
            timeval tv{0, 100000};
            if (select(sock_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) continue;
            ssize_t n = recvfrom(sock_, buf, sizeof(buf) - 1, 0, nullptr, nullptr);
            if (n <= 0) continue;
            buf[n] = '\0';
            if (onEvent_) onEvent_(std::string(buf));
        }
    });
    return true;
}

void EventBusClient::stop() {
    running_ = false;
    if (thr_.joinable()) thr_.join();
    if (sock_ >= 0) ::close(sock_);
}
