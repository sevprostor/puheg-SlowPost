#ifndef PROCESS_CPP
#define PROCESS_CPP
#include "process.h"
#include "config.h"
#include "log.h"

void RunningProcess::unhangProcess(uint64_t nowTime){

    if(nowTime - runningProc.lastActivity > config.unhangProcessTimeout && runningProc.running){

        runningProc.state = "FAIL";
        Log::warn("RProc", "Сброшено: runningProc.state = FAIL");

    }

}

#endif // PROCESS_CPP
