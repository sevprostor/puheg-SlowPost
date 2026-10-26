#ifndef CONFIG_H
#define CONFIG_H
#pragma once
#include <string>
#include <cstdint>
#include <vector>

#define MAX_CHUNKS 255

struct Config {



    uint16_t eventBusPort = 9400;
    std::string workDir = "./";
    std::string configPath = "pclient.conf";

    /////////////////////////////////////////////////
    /// \brief configVariables
    /// Параметры, которые будут искаться в .conf, по сути это
    /// имена переменных ниже.
    ///
    /// Потом можно придумать что половчее

    std::vector<std::string> configVariables = {
        "sendTXDelay",
        "maxProcessRetries",
        "sendRetryDelay",
        "sendProofDelay",
        "compressionLevel",
        "chunkSize",
        "maxTransfers"
    };

    ///////////////////////////////////////////////////////////
    ///
    /// ПАРАМЕТРЫ ПРОГРАММЫ
    ///

    int sendTXDelay = 30; // от 15 до 45 задержка между отправками, сек. С рандомизатором +-50%
    int maxProcessRetries = 7; // максимальное количество ретраев
    int sendRetryDelay = 90; // ретраи раз в 2 минуты
    int sendProofDelay = 120; // пруф раз в 3 минуты
    int compressionLevel = 6;
    uint16_t chunkSize = 1500;
    uint8_t maxTransfers = 4;

    uint16_t unhangBusyTimeout = 10; // таймаут отклечения DriverState.busy
    uint16_t unhangProcessTimeout = 240; // таймаут отклечения DriverState.busy
    //новое дописывать вниз, чтоы проще добавлять в configVariables

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
        uint64_t busySince;
        bool RX = false;
        bool TX = false;
        bool process;
        uint8_t avgspeed;
        uint16_t bytesSent;
        uint16_t bytesRcvd;
        uint16_t avgBps;
    } driverState;

    uint16_t proofTimeout = 1200; //20 минут ждем пруфа


    static bool unhangBusy(uint64_t);

    uint64_t nowEpoch();



};

extern Config config;
#endif // CONFIG_H
