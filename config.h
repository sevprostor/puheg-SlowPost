#ifndef CONFIG_H
#define CONFIG_H
#pragma once
#include <string>
#include <cstdint>
#include <vector>



struct Config {
    uint16_t eventBusPort = 9400;
    std::string workDir = "./";
    std::string configPath = "pclient.conf";

    // НОВОЕ: параметры отправки
    int sendTXDelay = 15000; // задержка между отправками (мс)
    int maxProcessRetries = 5; // максимальное количество ретраев
    int sendRetryDelay = 60; // ретраи раз в минуту
    int sendProofDelay = 300;
    uint16_t chunkSize = 2000;
    uint8_t maxTransfers = 4;

    struct abc{
        uint16_t mac;
        std::string ip;
        std::string key;
        uint8_t avgspeed;
        uint16_t bytesSent;
        uint16_t bytesRcvd;
        uint16_t avgBps;
    }cnt;

    abc myContact;
    std::vector<abc> addressbook;

    bool loadFromFile(const std::string& filename);
    bool parseCommandLine(int argc, char** argv);

    struct DriverState{
        bool busy = false;
        bool RX = false;
        bool TX = false;
        bool process;
        uint8_t avgspeed;
        uint16_t bytesSent;
        uint16_t bytesRcvd;
        uint16_t avgBps;
    } driverState;

    uint16_t proofTimeout = 1200; //20 минут ждем пруфа
};

extern Config config;
#endif // CONFIG_H
