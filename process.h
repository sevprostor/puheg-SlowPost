#ifndef PROCESS_H
#define PROCESS_H
#pragma once

//#include <vector>
//#include <iostream>
#include <string>
//#include <map>
#include <cstdint>
#include <chrono>


struct RunningProcess{
    uint32_t thread;
    std::string state;
    //uint16_t initiator = 0; //порт инициатора процесса, для адресной отправки еб-сообщений
    bool running = false;
    uint32_t processingUUID;

    //std::chrono::steady_clock::time_point startTime; // <-- ДОБАВЛЕНО
    std::chrono::steady_clock::time_point lastProcessCompleted; // <-- ДОБАВЛЕНО
    bool waitingApproove = false;

};

extern RunningProcess runningProc;

#endif // PROCESS_H
