#ifndef ADDRESS_H
#define ADDRESS_H
#pragma once

#include <string>
#include <cstdint>

class Address {
public:
    // Преобразование IP-адреса (10.0.x.x) в MAC (последние 2 октета)
    // Возвращает 0 при ошибке (неверный формат, не подсеть 10.x.x.x,
    // или второй октет не равен 0 — зарезервировано)
    static uint16_t ipToMac(const std::string& ipStr);

    // Обратное преобразование MAC в полный IP-адрес 10.0.x.x
    static std::string macToIp(uint16_t mac);
};
#endif // ADDRESS_H
