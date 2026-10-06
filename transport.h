#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <string>
#include "ebparser.h"

class Transport {
public:

    // Коллбэк для бинарных кадров (на будущее)
    using OnFrameCallback = std::function<void(const std::vector<uint8_t>&)>;
    // Коллбэк для JSON-событий (включая PW-кадры, завёрнутые в JSON)
    using OnEventCallback = std::function<void(const std::string&)>;

    bool init(uint16_t driverPort);
    void stop();

    // Отправка PW-кадра (listenwords не использует, но оставим для будущих передатчиков)
    bool sendFrame(const std::string& destIp, const std::vector<uint8_t>& payload);

    // Опрос сокета; вызывает onEvent_ для JSON и onFrame_ для бинарных данных
    EBMessage poll(int timeoutMs = 100);

    // Регистрация коллбэков
    void setOnFrame(OnFrameCallback cb) { onFrame_ = cb; }
    void setOnEvent(OnEventCallback cb) { onEvent_ = cb; }

private:
    int sock_ = -1;
    uint16_t driverPort_ = 9400;
    OnFrameCallback onFrame_;
    OnEventCallback onEvent_;

    // НОВОЕ: буфер приёма. Максимальный размер UDP-датаграммы 65507 байт,
    // берём с запасом 65536. Чанки по 2000 байт в JSON-обёртке дают ~10 КБ.
    std::vector<uint8_t> recvBuf_;
    //static bool inited_;
};
