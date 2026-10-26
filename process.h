#ifndef PROCESS_H
#define PROCESS_H
#pragma once

//#include <vector>
//#include <iostream>
#include <string>
//#include <map>
#include <cstdint>
//#include <chrono>
//#include <filesystem>
#include "file.h"


struct RunningProcess{
    uint32_t thread;
    std::string state;
    //uint16_t initiator = 0; //порт инициатора процесса, для адресной отправки еб-сообщений
    bool running = false;
    bool accepted = false;
    uint32_t processingUUID;
    fs::path processingFile;
    uint16_t peerMAC;
    uint32_t fileSize;
    char processingType;

    //std::chrono::steady_clock::time_point lastActivity; // <-- ДОБАВЛЕНО
    //std::chrono::steady_clock::time_point lastProcessCompleted; // <-- ДОБАВЛЕНО

    uint64_t startTime;
    uint64_t lastActivity;
    uint64_t lastProcessCompleted;

    bool waitingApproove = false;

    static void unhangProcess(uint64_t);

};

extern RunningProcess runningProc;

#endif // PROCESS_H
