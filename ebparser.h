#ifndef EVENTEX_H
#define EVENTEX_H
#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include "file.h"



struct EBMessage{
    uint16_t peer;
    std::string peerIp;

    bool binary = false;
    bool evenbus = false;
    bool words = false;

    std::string rawtext;
    std::vector<uint8_t> wordsframe;
};

class EBParser{
public:

    static void parseEmsg(const EBMessage&, File&);

    //обработка разных шинных сообщений
    static void wordsTopic(const EBMessage&, File&);
    static void processTopic(const EBMessage&, File&);
    static void netprofileTopic(const EBMessage&, File&);
    static void rxtxTopic(const EBMessage&);
    static void transportTopic(const EBMessage&, File&);


private:


};

#endif // EVENTEX_H
