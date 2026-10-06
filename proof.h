#ifndef PROOF_H
#define PROOF_H
#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "file.h"

class Proof {
public:
    // Создать и отправить пруф для одного чанка (первый чанок передачи)
    //static bool sendSingleProof(uint16_t senderMac, uint32_t uuid, int part);

    // Создать и отправить общий пруф для нескольких чанков
    //static bool sendBatchProof(uint16_t senderMac, uint32_t uuid, const std::vector<int>& parts);

    //bool parseProof(const std::vector<uint8_t>& data, PwProof& proof) const;

    static bool sendFileProof(std::string senderIp, uint32_t uuid, std::vector<int>& parts);
    //static bool sendFileProof(uint32_t uuid, std::vector<int>& parts);

    // НОВОЕ: обработка входящего пруфа (файл "PF")
    // Переносит подтверждённые чанки из waiting/ в approved/
    static bool processIncomingProof(uint32_t uuid, const std::vector<uint8_t>& approvedParts);



};
#endif // PROOF_H
