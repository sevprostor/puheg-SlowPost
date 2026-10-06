#include "words.h"
#include "log.h"
#include "config.h"


std::vector<uint8_t> Words::hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

size_t Words::parseEnv(const std::vector<uint8_t>& data, Envelope& env) const {

    // Парсит конверт пакета и возвращает длину конверта

    if (data.size() < 2) {
        Log::warn("Words", "Слишком короткий пакет: ", data.size(), " байт");
        return 0;
    }

    env.type = static_cast<char>(data[0]);
    uint8_t routeSize = data[1];

    size_t requiredSize = 2 + static_cast<size_t>(routeSize) * 2;
    if (data.size() < requiredSize) {
        Log::warn("Words", "Не хватает данных для маршрута: ", data.size(),
                  " из ", requiredSize);
        return 0;
    }

    env.route.clear();
    env.route.reserve(routeSize);

    size_t pos = 2;

    bool thisisnexthop = false;

    for (uint8_t i = 0; i < routeSize; ++i) {

        uint16_t addr = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
        env.route.push_back(addr);

        if(thisisnexthop){
            env.nexthop = addr;
            thisisnexthop = false;
        }

        // Здесь поставить флаг, что следующий в маршруте - nexthop,
        // который должен идти вслед за моим маком
        if(addr == config.myContact.mac) thisisnexthop = true;

        pos += 2;

    }

    env.fdest = env.route.back();

    return 1 + 1 + (routeSize * 2); // type(1), rsz(1), route(N * 2)
}



bool Words::parseFile(const std::vector<uint8_t>& data, size_t pos, PwFile& file) const {


    //size_t pos = 3; // сразу после Envelope

    // origin (2 байта)
    if (pos + 2 > data.size()) return false;
    file.origin = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
    pos += 2;

    // nameSize (1 байт)
    if (pos + 1 > data.size()) return false;
    uint8_t nameSize = data[pos++];

    // name (nameSize байт)
    if (pos + nameSize > data.size()) return false;
    file.name.assign(reinterpret_cast<const char*>(&data[pos]), nameSize);
    pos += nameSize;

    // 4. uuid (4 байта)

    // uuid (4 байта, network order)
    if (pos + 4 > data.size()) return false;
    file.uuid = static_cast<uint32_t>(
        (data[pos]     << 24) |
        (data[pos + 1] << 16) |
        (data[pos + 2] << 8)  |
        data[pos + 3]);
    pos += 4;


    // 5. totalParts и part


    if (pos + 2 > data.size()) return false;
    file.totalParts = data[pos++];
    file.part       = data[pos++];

    // 6. content — всё оставшееся
    file.content.assign(data.begin() + pos, data.end());
    return true;
}



bool Words::parseCommand(const std::vector<uint8_t>& data, PwCommand& cmd) const {
    //if (!parseEnvelope(data, cmd.env)) return false;
    // TODO: поля command
    return true;
}


std::vector<uint8_t> Words::buildMHFrame(const std::vector<uint16_t>& route, uint16_t origin, const std::string& name, uint32_t uuid, uint8_t totalParts, uint8_t part, const std::vector<uint8_t>& content){

    /*
    Структура пакета:

    Offset  Size    Field
    ------  ----    -----
    0       1       type = 'F'
    1       1       routeSize (количество узлов в маршруте)
    2       2*N     route[0..N-1] — MAC-адреса, каждый по 2 байта big-endian
    2+2N    2       origin (MAC изначального отправителя)
    4+2N    1       nameSize
    5+2N    N       name (имя файла)
    5+3N    4       uuid
    9+3N    1       totalParts

    */

    std::vector<uint8_t> frame;

    // Ограничение длины имени (поле 1 байт)
    size_t nameSize = name.size();
    if (nameSize > 255) nameSize = 255;

    size_t routeSize = route.size();
    if (routeSize > 255) routeSize = 255;

    // Резервируем место:
    // Envelope(type(1) + routeSize(1) + route(N*2)) +
    // Frame(origin(2) + nameSize(1) + name(N) + uuid(4) + parts(2) + content)

    frame.reserve(1 + 1 + (routeSize * 2) + 2 + 1 + nameSize + 4 + 2 + content.size());

    // === Envelope ===
    // type (1 байт)
    frame.push_back('F'); // MultihopFile

    // === Route (size 1 + route (адреса 16 бит, по 2 байта) ===
    frame.push_back(static_cast<uint8_t>(routeSize));

    // === ЦИКЛ ЗАПИСИ МАРШРУТА ===
    // Каждый адрес route[i] = uint16_t → 2 байта в big-endian
    for (size_t i = 0; i < routeSize; ++i) {
        uint16_t addr = route[i];
        frame.push_back(static_cast<uint8_t>((addr >> 8) & 0xFF)); // старший байт
        frame.push_back(static_cast<uint8_t>(addr & 0xFF));         // младший байт
    }

    /////////////////////////////////////////////////////////////////////
    /// По идее, именно отсюда должен начинаться шифрованный контент

    // === origin (2 байта, big-endian) ===
    frame.push_back(static_cast<uint8_t>((origin >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(origin & 0xFF));

    // === nameSize (1 байт) + name ===
    frame.push_back(static_cast<uint8_t>(nameSize));
    frame.insert(frame.end(), name.begin(), name.begin() + nameSize);

    // === uuid (4 байта, big-endian) ===
    frame.push_back(static_cast<uint8_t>((uuid >> 24) & 0xFF));
    frame.push_back(static_cast<uint8_t>((uuid >> 16) & 0xFF));
    frame.push_back(static_cast<uint8_t>((uuid >> 8)  & 0xFF));
    frame.push_back(static_cast<uint8_t>(uuid & 0xFF));

    // === totalParts (1 байт) + part (1 байт) ===
    frame.push_back(totalParts);
    frame.push_back(part);

    // === content ===
    frame.insert(frame.end(), content.begin(), content.end());

    return frame;
}


std::vector<uint8_t> Words::buildFileFrame(
    uint16_t finalDest,
    uint16_t origin,
    const std::string& name,
    uint32_t uuid,
    uint8_t totalParts,
    uint8_t part,
    const std::vector<uint8_t>& content)
{
    std::vector<uint8_t> frame;

    // Ограничение длины имени (поле 1 байт)
    size_t nameSize = name.size();
    if (nameSize > 255) nameSize = 255;

    // Резервируем место: Envelope(3) + origin(2) + nameSize(1) + name(N) + uuid(4) + parts(2) + content
    frame.reserve(3 + 2 + 1 + nameSize + 4 + 2 + content.size());

    // === Envelope ===
    // type (1 байт)
    frame.push_back('F');
    // finalDest (2 байта, network order / big-endian)
    frame.push_back(static_cast<uint8_t>((finalDest >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(finalDest & 0xFF));

    // === origin (2 байта, big-endian) ===
    frame.push_back(static_cast<uint8_t>((origin >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(origin & 0xFF));

    // === nameSize (1 байт) + name ===
    frame.push_back(static_cast<uint8_t>(nameSize));
    frame.insert(frame.end(), name.begin(), name.begin() + nameSize);

    // === uuid (4 байта, big-endian) ===
    frame.push_back(static_cast<uint8_t>((uuid >> 24) & 0xFF));
    frame.push_back(static_cast<uint8_t>((uuid >> 16) & 0xFF));
    frame.push_back(static_cast<uint8_t>((uuid >> 8)  & 0xFF));
    frame.push_back(static_cast<uint8_t>(uuid & 0xFF));

    // === totalParts (1 байт) + part (1 байт) ===
    frame.push_back(totalParts);
    frame.push_back(part);

    // === content ===
    frame.insert(frame.end(), content.begin(), content.end());

    return frame;
}
