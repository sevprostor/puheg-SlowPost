#ifndef STATS_H
#define STATS_H

#pragma once

#include <string>
#include <cstdint>
#include <vector>

struct PwFile;

struct TRXstats {

    // Идентификаторы передачи
    uint32_t uuid = 0;
    std::string peerIp;
    std::string nextHopIp;
    std::string lastHopIp;
    std::string fileName;

    // Временные метки и длительности
    // время начала передачи, приведшей к появлению первого чанка. Переделать на время хроно
    uint64_t startTime = 0;
    uint64_t endTime = 0;

    //uint64_t totalUptime = 0; // общая длительность активного приема (RX), секунды

    // Объём данных
    uint64_t bytesRcvd = 0; // всего байт принято
    uint64_t bytesSent = 0; // всего байт отправлено

    // Временные характеристики
    //uint32_t сhunkToChunkPeriod = 0; // среднее время между приходом чанков, сек
    //uint32_t сhunkDuration = 0; // средняя длительность приема чанка, сек

    // Скоростные характеристики
    uint8_t speedAbs = 0; // средняя мгновенная скорость 1-10, из сообщений транспорта
    uint16_t speedBS = 0; // средняя мгновенная скорость в байт/сек
    //uint16_t realSpeedBS = 0; // средняя скорость с учетом всех простоев (все время передачи)

    std::vector<uint16_t> route;
    uint8_t tries = 0;
    uint8_t successes = 0;


};

class Statistics {
public:
    // Статические объекты статистики текущих передач
    static TRXstats transmissionStats;

    // Создание или обновление файла статистики для входящей передачи.
    // При первом чанке передачи создаёт tmp/stats, при последующих — обновляет его.
    static void collectIncomingTransportStats(uint32_t tid, std::string peer, uint8_t absSpeed);
    static void updateIncomingStats(const PwFile& pwFile);
    //static void finalizeIncomingStats(uint32_t uuid, const std::string& peerIp);
    static void resetRXstats();

    static void createOutgoingStats(uint32_t uuid, const std::string& peer, const std::string& filename);
    static void collectOutgoingTransportStats(uint32_t tid, std::string peer, uint8_t absSpeed);
    static void updateOutgoingStats();
    //static void finalizeOutgingStats(uint32_t uuid, const std::string& peerIp);

    static void finalizeStats(uint32_t uuid, const std::string& peerIp, bool uplink);

};

//extern FileStats transportRXstats;
//extern FileStats transportTXstats;

#endif // STATS_H
