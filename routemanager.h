#ifndef ROUTEMANAGER_H
#define ROUTEMANAGER_H

#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;

struct RouteRecord {
    uint64_t timestamp;
    std::vector<uint16_t> chain;
    int attempts;
    int successes;

    double rate() const {
        if (attempts == 0) return 0.0;
        return static_cast<double>(successes) / attempts;
    }
};

class RouteManager {
public:
    // Инициализация: создаёт файл с 1-хоповыми маршрутами, если его нет
    bool initRoutesFile();

    // Выбор маршрута до получателя
    std::vector<uint16_t> findRouteTo(uint16_t dest);

    // Маршрут для передачи в spool/<peerIp>/<uuid>/tmp/route
    // Сохранение, чтение
    void saveIncomingRoute(uint32_t origin, std::vector<uint16_t> route, uint32_t uuid);
    bool saveRouteForTransfer(uint16_t destMac, uint32_t uuid);
    bool saveRouteForProof(uint16_t destMac, uint32_t uuid);

    std::vector<uint16_t> getRouteForTransfer(uint16_t destMac, uint32_t uuid);
    std::vector<uint16_t> getRouteForReply(uint32_t uuid);

    // Обновление счётчиков при отправке чанка (попытка)
    void incrementAttempts(const std::string& destIp);

    // Обновление счётчиков при приходе пруфа (успех)
    void incrementSuccesses(const std::string& destIp);

    // Копирование маршрута передачи в общий файл по завершении
    void commitRoute(const std::string& destIp);

private:
    bool parseRouteLine(const std::string& line, RouteRecord& rec);
    bool hasLoop(const std::vector<uint16_t>& chain);
    uint64_t currentTimestamp();
    uint64_t getLastTimestampFromRoutes();
    void updateRouteFile(const std::string& destIp, bool success);
};
#endif // ROUTEMANAGER_H
