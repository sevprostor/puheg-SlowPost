#ifndef PROOF_H
#define PROOF_H
#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "file.h"

class Proof {
public:


    ///////////////////////////////////////////
    /// перенести в File

    static bool getTransferDir(uint32_t uuid, fs::path& transferDir);
    static void cleanSpool(uint32_t uuid);
    static void cleanInbox(uint32_t uuid);
    static void moveToSent(uint32_t uuid);

    /// Оставить тут
    static void getOriginalFName(uint32_t uuid, std::string& original);
    static void approveChunks(uint32_t uuid, std::vector<uint8_t> approvedChunks);
    static bool isTransmissionCompleted(uint32_t uuid);
    /////////////////////////////////////////////

    static bool sendFileProof(std::string senderIp, uint32_t uuid, std::vector<int>& parts);


    // Переносит подтверждённые чанки из waiting/ в approved/
    static bool processIncomingProof(uint32_t uuid, const std::vector<uint8_t>& approvedParts);



};
#endif // PROOF_H
