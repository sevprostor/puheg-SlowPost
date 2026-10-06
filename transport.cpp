#include "transport.h"
#include "log.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstdio>
#include <cstring>

//bool Transport::inited_;

bool Transport::init(uint16_t driverPort) {
    recvBuf_.resize(65536);

    driverPort_ = driverPort;
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(sock_); sock_ = -1;
        return false;
    }

    socklen_t len = sizeof(addr);
    getsockname(sock_, (sockaddr*)&addr, &len);
    uint16_t myLocalPort = ntohs(addr.sin_port);

    sockaddr_in bus{};
    bus.sin_family = AF_INET;
    bus.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bus.sin_port = htons(driverPort_);

    // Подписка на нужные ключи верхнего уровня
    std::string sub =
        "{\"subscribe\": [\"words\", \"transport\", \"process\", \"netprofile\", \"tx\", \"rx\"], \"name\":\"slowpost\"}";
    sendto(sock_, sub.c_str(), sub.size(), 0, (sockaddr*)&bus, sizeof(bus));


    return true;
}
void Transport::stop() {
    if (sock_ >= 0) { ::close(sock_); sock_ = -1; }
}

#include "transport.h"
#include "log.h"   // <-- добавляем
// ... остальные инклуды ...

bool Transport::sendFrame(const std::string& destIp, const std::vector<uint8_t>& payload) {
    in_addr addr;
    if (inet_pton(AF_INET, destIp.c_str(), &addr) != 1) {
        Log::error("Transport", "Неверный IP-адрес: ", destIp); // было fprintf
        return false;
    }
    // ... остальное без изменений ...
    // Конвертируем IP в 4 байта network order
    //in_addr addr;
    if (inet_pton(AF_INET, destIp.c_str(), &addr) != 1) {
        fprintf(stderr, "Ошибка: неверный IP-адрес %s\n", destIp.c_str());
        return false;
    }





    // Пакет: [4 байта IP] + [payload с магиком PW\x01]
    std::vector<uint8_t> packet;
    packet.reserve(4 + payload.size());
    const uint8_t* ipBytes = reinterpret_cast<const uint8_t*>(&addr.s_addr);
    packet.insert(packet.end(), ipBytes, ipBytes + 4);
    packet.insert(packet.end(), payload.begin(), payload.end());

    sockaddr_in bus{};
    bus.sin_family = AF_INET;
    bus.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bus.sin_port = htons(driverPort_);

    ssize_t n = sendto(sock_, packet.data(), packet.size(), 0, (sockaddr*)&bus, sizeof(bus));
    return n == static_cast<ssize_t>(packet.size());
}

EBMessage Transport::poll(int timeoutMs) {

    EBMessage justRecv;
    //Пусть он делает сразу целый объект EBMsg c разобранным json внутри

    if (sock_ < 0) return justRecv;
    fd_set rfds; FD_ZERO(&rfds); FD_SET(sock_, &rfds);
    timeval tv{0, timeoutMs * 1000};
    if (select(sock_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) return justRecv;

    // НОВОЕ: читаем в большой буфер вместо локальных 2048 байт
    ssize_t n = recvfrom(sock_, recvBuf_.data(), recvBuf_.size(), 0, nullptr, nullptr);

    //ssize_t n = recvfrom(sock_, buf, sizeof(buf), 0, nullptr, nullptr);
    if (n <= 0) return justRecv;



    if (recvBuf_[0] == '{') {
        justRecv.evenbus = true;

        // ВАЖНО: строка ровно из n байт! Иначе хвост предыдущей датаграммы
        // (старые "words", хвосты JSON) прилипает к текущему сообщению
        justRecv.rawtext.assign(reinterpret_cast<const char*>(recvBuf_.data()), static_cast<size_t>(n));

        //Log::info("Transport", "json rcvd: ", justRecv.rawtext);
    } else {
        // Бинарный кадр — тоже копируем ровно n байт
        justRecv.binary = true;
        justRecv.wordsframe.assign(recvBuf_.begin(), recvBuf_.begin() + n);
    }

    return justRecv;
}
