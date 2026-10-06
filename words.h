#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ===========================================================================
// СТРУКТУРЫ — только данные, без логики
// ===========================================================================

// Общий заголовок любого PW-пакета (после магика PW\x01):
//   type   (1 байт) — '0' proof | 'F' file | 'C' command
//   sender (2 байта) — последние октеты IP = MAC

/////////////////////////////////////////////////////////////////////////////////
// ПЕРЕДЕЛАТЬ НА ЭТО!
// type (1) | [routeSize(1), nexthop(2), (nexthop...)] (min 3) | - Total min 4B


struct Envelope {

    char type = 0;

    std::vector <uint16_t> route;

    uint16_t lasthop = 0;
    uint16_t nexthop = 0;
    uint16_t fdest = 0;

};

// Пакет типа 'F' — чанк файла:
//   origin (2 байта) | nameSize (1 байт) | name (nameSize) | uuid (3) | content (...)
struct PwFile {
    Envelope   env;

    uint16_t origin; // адрес изначального отправителя

    //uint8_t    proofType  = 0; // убрать
    std::string name;
    uint32_t   uuid       = 0;
    uint8_t    totalParts = 0;
    uint8_t    part       = 0;
    std::vector<uint8_t> content;

};

struct PwProof   {
    Envelope env;
    std::string name;
    uint32_t uuid = 0; // переделать на полные 32 бит - 4 байта
    std::vector<uint8_t> content; //номера частей, которые подтверждаются этим пруфом
};
struct PwCommand { Envelope env; /* TODO */ };

// ===========================================================================
// КЛАСС — методы разбора; заполняют структуры по ссылке
// ===========================================================================

class Words {
public:
    // Разбор общего заголовка: type (1) + sender (2)
    //bool parseEnvelope(const std::vector<uint8_t>& data, Envelope& env) const;

    size_t parseEnv(const std::vector<uint8_t>& data, Envelope& env) const;

    // Разбор кадра типа 'F' (включая envelope -> file.env)
    bool parseFile(const std::vector<uint8_t>& data, size_t pos, PwFile& file) const;

    // Заглушки на будущее
    //bool parseProof(const std::vector<uint8_t>& data, PwProof& proof) const;
    bool parseCommand(const std::vector<uint8_t>& data, PwCommand& cmd) const;

    // Утилита: hex-строка -> байты
    static std::vector<uint8_t> hexToBytes(const std::string& hex);

    static std::vector<uint8_t> buildMHFrame(
        const std::vector<uint16_t>& route,
        uint16_t origin,
        const std::string& name,
        uint32_t uuid,
        uint8_t totalParts,
        uint8_t part,
        const std::vector<uint8_t>& content);


    static std::vector<uint8_t> buildFileFrame(
        uint16_t finalDest,
        uint16_t origin,
        const std::string& name,
        uint32_t uuid,
        uint8_t totalParts,
        uint8_t part,
        const std::vector<uint8_t>& content);
};
