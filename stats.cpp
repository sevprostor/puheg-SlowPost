#include "stats.h"
#include "words.h"
#include "config.h"
#include "log.h"
#include "address.h"
#include "file.h"
#include "libs/json.hpp"
#include "routemanager.h"
#include "process.h"

#include <filesystem>
#include <fstream>
#include <chrono>

using json = nlohmann::json;
namespace fs = std::filesystem;

TRXstats Statistics::transmissionStats;




void Statistics::updateOutgoingStats(){

    /////////////////////////////////////////////////////
    ///
    /// Записывает в исходящую статистику из transmissionStats
    /// и runningProc
    ///

    // Делаем, только если процесс закончился
    if(runningProc.running) return;

    std::string peer = Address::macToIp(runningProc.peerMAC);


    File file;


    RouteManager router;

    fs::path uuidDir = file.getSpoolDir(peer, runningProc.processingUUID);
    //проверить существование директории. Если нет, то выйти.
    if (!fs::exists(uuidDir)) {
        Log::warn("Stats", "Каталог otbox/ не найден. Выход.");
        return;
    }

    fs::path tmpDir = uuidDir / "tmp";
    fs::path statsFile = tmpDir / "stats";

    std::error_code ec;

    uint64_t chunkDuration = config.nowEpoch() - runningProc.startTime;
    if(chunkDuration > 0) transmissionStats.speedBS = static_cast<uint16_t>(runningProc.fileSize / chunkDuration);

    transmissionStats.bytesSent = (runningProc.state == "FAIL")? 0 : runningProc.fileSize;

    json statsJson;

    if (fs::exists(statsFile, ec)) {

        // Читаем существующие данные из файла
        std::ifstream in(statsFile.string());
        json existingStats;
        if (in) {
            existingStats = json::parse(in, nullptr, false);
            in.close();
        }

        if (!existingStats.is_discarded()) {

            // === totalBytesRcvd: приплюсовываем ===
            uint64_t existingSentBytes = existingStats.value("totalBytesSent", (uint64_t)0);
            statsJson["totalBytesSent"] = existingSentBytes + transmissionStats.bytesSent;

            // === startTime: берём из файла (он там с самого первого чанка) ===
            uint64_t startTime = existingStats.value("startTime", (uint64_t)0);
            statsJson["startTime"] = startTime;

            // === totalUptimeDuration: общая длительность чанков ===
            uint64_t totalUptime = existingStats.value("totalUptimeDuration", (uint64_t)0);
            statsJson["totalUptimeDuration"] = totalUptime + chunkDuration;

            // === avgSpeedAbs: усредняем между значением из файла и текущим ===
            uint8_t existingSpeed = existingStats.value("avgSpeedAbs", (uint8_t)0);
            statsJson["avgSpeedAbs"] = static_cast<uint8_t>(
                (existingSpeed + transmissionStats.speedAbs) / 2);

            // === avgSpeedBS: пересчитываем по новым значениям ===
            uint16_t avgSpeedBS = existingStats.value("avgSpeedBS", (uint16_t)0);
            if(avgSpeedBS == 0) statsJson["avgSpeedBS"] = transmissionStats.speedBS;
            statsJson["avgSpeedBS"] = (avgSpeedBS + transmissionStats.speedBS) / 2;

            statsJson["nextHopIp"] = transmissionStats.nextHopIp;

            statsJson["route"] = existingStats["route"];


        } else {
            Log::warn("Stats", "⚠️ Файл tmp/stats повреждён, перезаписываю");
        }
    }

    Log::debug("Stats", "-------- Записываю статистику #", std::to_string(runningProc.processingUUID)," --------");
    Log::debug("Stats", statsJson.dump(), "\n");


    // 4. перезаписываем файл
    std::ofstream out(statsFile.string(), std::ios::trunc);
    if (!out) {
        Log::error("Stats", "Не удалось открыть tmp/stats для записи");
        return;
    }
    out << statsJson.dump(2);
    out.close();


    Log::debug("Stats", "Обновлён tmp/stats для uuid=", runningProc.processingUUID,
                   " (+", runningProc.fileSize, " байт)");

}

void Statistics::createOutgoingStats(uint32_t uuid, const std::string& peer, const std::string& filename){

    //////////////////////////////////////////////////
    ///
    /// Первоначальное создание stats для исходящей передачи.
    /// Следующий шаг произойдет после отправки чанка - updateOutgoingStats()
    ///

    File file;
    RouteManager router;

    fs::path uuidDir = file.getSpoolDir(peer, uuid);

    //проверить существование директории. Если нет, то выйти.
    if (!fs::exists(uuidDir)) {
        Log::warn("Stats", "Каталог otbox/ не найден. Выход.");
        return;
    }

    fs::path tmpDir = uuidDir / "tmp";
    fs::path statsFile = tmpDir / "stats";

    std::error_code ec;



    json statsJson;
    statsJson["uuid"] = uuid;
    statsJson["fileName"] = filename;
    statsJson["peerIp"] = peer;
    statsJson["startTime"] = config.nowEpoch();
    statsJson["route"] = router.getRouteForTransfer(Address::ipToMac(peer), uuid);


    fs::create_directories(tmpDir, ec);
    if (ec) {
        Log::error("Stats", "❌ Не удалось создать каталог tmp/: ", ec.message());
        return;
    }

    Log::debug("Stats", "-------- Записываю статистику #",std::to_string(uuid)," --------");
    Log::debug("Stats", "startTime:", statsJson["startTime"].dump());
    Log::debug("Stats", "peerIp:", statsJson["peerIp"].dump());
    Log::debug("Stats", "fileName:", statsJson["fileName"].dump(), "\n");


    // 4. Записываем (перезаписываем) файл
    std::ofstream out(statsFile.string(), std::ios::trunc);
    if (!out) {
        Log::error("Stats", "❌ Не удалось открыть tmp/stats для записи");
        return;
    }
    out << statsJson.dump(2);
    out.close();



}

void Statistics::collectOutgoingTransportStats(uint32_t tid, std::string peer, uint8_t absSpeed){

    static uint32_t lastTid;

    if(lastTid != tid){

        // Началась новая передача

        lastTid = tid;
        TRXstats newStats;


        Log::info("Stats", "-------- Uplink #", tid, " --------");
        newStats.startTime = config.nowEpoch();
        newStats.nextHopIp = peer;
        transmissionStats = newStats;

    }

    transmissionStats.speedAbs = absSpeed;

}

void Statistics::collectIncomingTransportStats(uint32_t tid, std::string peer, uint8_t absSpeed){

    static uint32_t lastTid;

    if(lastTid != tid){

        // Началась новая передача

        lastTid = tid;
        TRXstats newStats;


        Log::info("Stats", "-------- Downlink #", tid, " --------");
        newStats.startTime = config.nowEpoch();
        newStats.lastHopIp = peer;
        transmissionStats = newStats;

    }

    transmissionStats.speedAbs = absSpeed;

}

void Statistics::updateIncomingStats(const PwFile& pwFile) {


    std::string senderIp = Address::macToIp(pwFile.origin);

    fs::path uuidDir = fs::path(config.workDir) /
                       (config.myContact.ip.empty() ? "unknown" : config.myContact.ip) /
                       "inbox" / senderIp / std::to_string(pwFile.uuid);

    fs::path tmpDir = uuidDir / "tmp";
    fs::path statsFile = tmpDir / "stats";

    std::error_code ec;

    // 1. Сохраняем данные только что пришедшего чанка

    //auto now = std::chrono::system_clock::now();
    //uint64_t nowTime = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(now));
    //uint64_t chunkDuration = nowTime - transmissionStats.startTime;
    uint64_t chunkDuration = config.nowEpoch() - transmissionStats.startTime;

    transmissionStats.bytesRcvd += pwFile.content.size();

    //if (transportRXstats.totalWorkDuration > 0) {
    if (chunkDuration > 0) {
        transmissionStats.speedBS = static_cast<uint16_t>(
            transmissionStats.bytesRcvd / chunkDuration);
    }



    json statsJson;
    statsJson["uuid"]              = pwFile.uuid;
    statsJson["fileName"]          = pwFile.name;
    statsJson["peerIp"]            = senderIp;
    statsJson["startTime"]         = transmissionStats.startTime;
    statsJson["lastHopIp"]         = transmissionStats.lastHopIp;
    statsJson["avgSpeedAbs"]       = transmissionStats.speedAbs;
    statsJson["avgSpeedBS"]        = transmissionStats.speedBS;
    statsJson["totalUptimeDuration"] = chunkDuration;
    statsJson["totalBytesRcvd"]    = transmissionStats.bytesRcvd;
    statsJson["route"]             = pwFile.env.route;

    // 2. Проверить, существует ли tmp/stats
    //    - Если да, то открыть, прочитать, приплюсовать (усреднить) новые данные с данными из файла.
    //    - Если нет, то просто записать.
    // 2. Проверить, существует ли tmp/stats
    //    - Если да, то открыть, прочитать, приплюсовать (усреднить) новые данные с данными из файла.
    //    - Если нет, то просто записать.

    if (fs::exists(statsFile, ec)) {

        // Читаем существующие данные из файла
        std::ifstream in(statsFile.string());
        json existingStats;
        if (in) {
            existingStats = json::parse(in, nullptr, false);
            in.close();
        }

        if (!existingStats.is_discarded()) {

            // === totalBytesRcvd: приплюсовываем ===
            uint64_t existingBytes = existingStats.value("totalBytesRcvd", (uint64_t)0);
            statsJson["totalBytesRcvd"] = existingBytes + pwFile.content.size();

            // === startTime: берём из файла (он там с самого первого чанка) ===
            uint64_t startTime = existingStats.value("startTime", (uint64_t)0);
            statsJson["startTime"] = startTime;

            // === totalWorkDuration: общая длительность чанков ===
            // Перод активной работы начинается с началом приема и заканчивается тут
            //uint64_t newDuration = nowTime - updateTime;
            uint64_t totalUptime = existingStats.value("totalUptimeDuration", (uint64_t)0);
            statsJson["totalUptimeDuration"] = totalUptime + chunkDuration;

            // === avgSpeedAbs: усредняем между значением из файла и текущим ===
            uint8_t existingSpeed = existingStats.value("avgSpeedAbs", (uint8_t)0);
            statsJson["avgSpeedAbs"] = static_cast<uint8_t>(
                (existingSpeed + transmissionStats.speedAbs) / 2);

            // === avgSpeedBS: пересчитываем по новым значениям ===
            uint16_t avgSpeedBS = existingStats.value("avgSpeedBS", (uint16_t)0);
            if(avgSpeedBS == 0) statsJson["avgSpeedBS"] = transmissionStats.speedBS;
            statsJson["avgSpeedBS"] = (avgSpeedBS + transmissionStats.speedBS) / 2;

            // === lastHopIp, route, fileName, peerIp: берём из файла
            //     (они константны в рамках одной передачи, но файл — источник истины)
            if (existingStats.contains("lastHopIp")) {
                statsJson["lastHopIp"] = existingStats["lastHopIp"];
            }
            if (existingStats.contains("route")) {
                statsJson["route"] = existingStats["route"];
            }

        //Log::debug("Stats", "updateIncomingStats");

        } else {
            Log::warn("Stats", "⚠️ Файл tmp/stats повреждён, перезаписываю");
        }
    }

    // 3. Создаём каталог tmp/ если его ещё нет
    fs::create_directories(tmpDir, ec);
    if (ec) {
        Log::error("Stats", "❌ Не удалось создать каталог tmp/: ", ec.message());
        return;
    }

    Log::debug("Stats", "-------- Записываю статистику #",std::to_string(pwFile.uuid)," --------");
    Log::debug("Stats", statsJson.dump(), "\n");


    // 4. Записываем (перезаписываем) файл
    std::ofstream out(statsFile.string(), std::ios::trunc);
    if (!out) {
        Log::error("Stats", "❌ Не удалось открыть tmp/stats для записи");
        return;
    }
    out << statsJson.dump(2);
    out.close();

    // Логирование
    bool isFirstChunk = !fs::exists(statsFile, ec) ||
                        (statsJson["totalBytesRcvd"] == pwFile.content.size());
    if (isFirstChunk) {
        Log::info("Stats", "✓ Создан tmp/stats для uuid=", pwFile.uuid);
    } else {
        Log::debug("Stats", "✓ Обновлён tmp/stats для uuid=", pwFile.uuid,
                   " (+", pwFile.content.size(), " байт)");
    }
}


/*
void Statistics::finalizeIncomingStats(uint32_t uuid, const std::string& peerIp) {

    std::string uuidStr = std::to_string(uuid);
    std::error_code ec;

    // Пути
    fs::path baseDir = fs::path(config.workDir) /
                       (config.myContact.ip.empty() ? "unknown" : config.myContact.ip);

    fs::path uuidDir = baseDir / "inbox" / peerIp / uuidStr;
    fs::path tmpDir  = uuidDir / "tmp";
    fs::path statsFile = tmpDir / "stats";
    fs::path routeFile = tmpDir / "route";
    fs::path historyDir = baseDir / "inbox" / "history";

    // Проверка наличия tmp/stats
    if (!fs::exists(statsFile, ec)) {
        Log::warn("Stats", "⚠️ tmp/stats не найден для uuid=", uuid, ", финализация невозможна");
        return;
    }

    // Читаем накопленную статистику
    std::ifstream statsIn(statsFile.string());
    if (!statsIn) {
        Log::error("Stats", "❌ Не удалось открыть tmp/stats для чтения");
        return;
    }

    json finalStats;
    try {
        finalStats = json::parse(statsIn);
    } catch (const std::exception& e) {
        Log::error("Stats", "❌ Ошибка парсинга tmp/stats: ", e.what());
        return;
    }
    statsIn.close();

    // === Дополнение: время окончания ===
    auto now = std::chrono::system_clock::now();
    uint64_t endTime = static_cast<uint64_t>(
        std::chrono::system_clock::to_time_t(now));
    finalStats["endTime"] = endTime;

    // === Дополнение: точный размер собранного файла ===
    std::string fileName = finalStats.value("fileName", "");
    if (!fileName.empty()) {
        fs::path finalFile = baseDir / "inbox" / peerIp / fileName;
        if (fs::exists(finalFile, ec)) {
            uint64_t fileSize = fs::file_size(finalFile, ec);
            finalStats["totalBytesRcvd"] = fileSize;
        }
    }

    // === Дополнение: peerIp ===
    finalStats["peerIp"] = peerIp;

    // === Дополнение: данные из tmp/route (маршрут, tries, successes) ===
    if (fs::exists(routeFile, ec)) {
        std::ifstream routeIn(routeFile.string());
        if (routeIn) {
            std::string routeLine;
            std::getline(routeIn, routeLine);
            routeIn.close();

            // Парсим: "YYYYMMDDHHMMSS mac1,mac2,... N/M"
            size_t firstSpace = routeLine.find(' ');
            size_t lastSpace  = routeLine.rfind(' ');

            if (firstSpace != std::string::npos &&
                lastSpace  != std::string::npos &&
                firstSpace != lastSpace) {

                // Цепочка адресов
                std::string routeStr = routeLine.substr(
                    firstSpace + 1, lastSpace - firstSpace - 1);

                json routeArray = json::array();
                std::istringstream iss(routeStr);
                std::string token;
                while (std::getline(iss, token, ',')) {
                    try {
                        routeArray.push_back(std::stoul(token));
                    } catch (...) {}
                }
                finalStats["route"] = routeArray;

                // N/M
                std::string statStr = routeLine.substr(lastSpace + 1);
                size_t slash = statStr.find('/');
                if (slash != std::string::npos) {
                    try {
                        finalStats["tries"]     = std::stoi(statStr.substr(0, slash));
                        finalStats["successes"] = std::stoi(statStr.substr(slash + 1));
                    } catch (...) {}
                }
            }
        }
    }

    // === Вычисление финальных скоростей ===
    uint64_t totalBytes = finalStats.value("totalBytesRcvd", (uint64_t)0);
    uint64_t workDuration = finalStats.value("totalWorkDuration", (uint64_t)0);

    if (workDuration > 0) {
        finalStats["avgSpeedBS"] = static_cast<uint16_t>(totalBytes / workDuration);
    }



    uint64_t startTime = finalStats.value("startTime", (uint64_t)0);
    if (startTime > 0 && endTime > startTime) {
        uint64_t totalDuration = endTime - startTime;

        //Реальная эффективная скорость с учетом сжатия.
        finalStats["realSpeedBS"] = static_cast<uint16_t>(totalBytes / totalDuration);
        finalStats["totalDuration"] = totalDuration;
    }

    // === Каталог history/ ===
    fs::create_directories(historyDir, ec);
    if (ec) {
        Log::error("Stats", "❌ Не удалось создать inbox/history/: ", ec.message());
        return;
    }

    // === Перемещение tmp/stats → inbox/history/<uuid> (БЕЗ расширения) ===
    fs::path historyFile = historyDir / uuidStr;

    // Сначала записываем финальный JSON
    std::ofstream historyOut(historyFile.string(), std::ios::trunc);
    if (!historyOut) {
        Log::error("Stats", "❌ Не удалось создать ", historyFile.string());
        return;
    }
    historyOut << finalStats.dump(2);
    historyOut.close();

    // Удаляем tmp/stats
    fs::remove(statsFile, ec);

    Log::info("Stats", "✓ Статистика финализирована для uuid=", uuid,
              ": ", historyFile.string());
}
*/

void Statistics::finalizeStats(uint32_t uuid, const std::string& peerIp, bool uplink) {


    std::string uuidStr = std::to_string(uuid);
    std::error_code ec;

    fs::path baseDir = fs::path(config.workDir) / (config.myContact.ip.empty() ? "unknown" : config.myContact.ip);
    fs::path historyDir = uplink ? baseDir / "outbox" / "history" : baseDir / "inbox" / "history";

    fs::path uuidDir;
    fs::path tmpDir;
    fs::path statsFile;
    fs::path routeFile;


    // Пути
    if(uplink){

        if(!File::getSpoolPeerDir(uuid, uuidDir)) return;

    }else{

        uuidDir = baseDir / "inbox" / peerIp / uuidStr;


    }

    tmpDir  = uuidDir / "tmp";
    statsFile = tmpDir / "stats";
    routeFile = tmpDir / "route";

    // Проверка наличия tmp/stats
    if (!fs::exists(statsFile, ec)) {
        Log::warn("Stats", "⚠️ tmp/stats не найден для uuid=", uuid, ", финализация невозможна");
        return;
    }

    // Читаем накопленную статистику
    std::ifstream statsIn(statsFile.string());
    if (!statsIn) {
        Log::error("Stats", "❌ Не удалось открыть tmp/stats для чтения");
        return;
    }

    json finalStats;
    try {
        finalStats = json::parse(statsIn);
    } catch (const std::exception& e) {
        Log::error("Stats", "❌ Ошибка парсинга tmp/stats: ", e.what());
        return;
    }
    statsIn.close();

    // === Дополнение: время окончания ===
    //auto now = std::chrono::system_clock::now();
    //uint64_t endTime = static_cast<uint64_t>(
    //    std::chrono::system_clock::to_time_t(now));

    finalStats["endTime"] = config.nowEpoch();

    // === Дополнение: точный размер собранного файла ===
    std::string fileName = finalStats.value("fileName", "");

    if(!fileName.empty()){

        if (uplink){

            //uint64_t totBytes = finalStats.value("totalBytesSent", (uint64_t)0);
            //finalStats["totalBytesSent"] = totBytes;

        } else {

            fs::path finalFile = baseDir / "inbox" / peerIp / fileName;

            if (fs::exists(finalFile, ec)) {
                uint64_t fileSize = fs::file_size(finalFile, ec);
                finalStats["totalBytesRcvd"] = fileSize;
            }
        }
    }

    // === Дополнение: peerIp ===
    finalStats["peerIp"] = peerIp;

    // === Дополнение: данные из tmp/route (маршрут, tries, successes) ===
    if (fs::exists(routeFile, ec)) {
        std::ifstream routeIn(routeFile.string());
        if (routeIn) {
            std::string routeLine;
            std::getline(routeIn, routeLine);
            routeIn.close();

            // Парсим: "YYYYMMDDHHMMSS mac1,mac2,... N/M"
            size_t firstSpace = routeLine.find(' ');
            size_t lastSpace  = routeLine.rfind(' ');

            if (firstSpace != std::string::npos &&
                lastSpace  != std::string::npos &&
                firstSpace != lastSpace) {

                // Цепочка адресов
                std::string routeStr = routeLine.substr(
                    firstSpace + 1, lastSpace - firstSpace - 1);

                json routeArray = json::array();
                std::istringstream iss(routeStr);
                std::string token;
                while (std::getline(iss, token, ',')) {
                    try {
                        routeArray.push_back(std::stoul(token));
                    } catch (...) {}
                }
                finalStats["route"] = routeArray;

                // N/M
                std::string statStr = routeLine.substr(lastSpace + 1);
                size_t slash = statStr.find('/');
                if (slash != std::string::npos) {
                    try {
                        finalStats["tries"]     = std::stoi(statStr.substr(0, slash));
                        finalStats["successes"] = std::stoi(statStr.substr(slash + 1));
                    } catch (...) {}
                }
            }
        }
    }

    // === Вычисление финальных скоростей ===
    uint64_t totalBytes;
    uint64_t workDuration = finalStats.value("totalWorkDuration", (uint64_t)0);

    if(uplink) finalStats.value("totalBytesSent", (uint64_t)0);
    else finalStats.value("totalBytesRcvd", (uint64_t)0);


    if (workDuration > 0) {
        finalStats["avgSpeedBS"] = static_cast<uint16_t>(totalBytes / workDuration);
    }



    uint64_t startTime = finalStats.value("startTime", (uint64_t)0);
    if (startTime > 0 && config.nowEpoch() > startTime) {
        uint64_t totalDuration = config.nowEpoch() - startTime;

        //Реальная эффективная скорость с учетом сжатия.
        finalStats["realSpeedBS"] = static_cast<uint16_t>(totalBytes / totalDuration);
        finalStats["totalDuration"] = totalDuration;
    }

    // === Каталог history/ ===
    fs::create_directories(historyDir, ec);
    if (ec) {
        Log::error("Stats", "❌ Не удалось создать inbox/history/: ", ec.message());
        return;
    }

    // === Перемещение tmp/stats → inbox/history/<uuid> (БЕЗ расширения) ===
    fs::path historyFile = historyDir / uuidStr;

    // Сначала записываем финальный JSON
    std::ofstream historyOut(historyFile.string(), std::ios::trunc);
    if (!historyOut) {
        Log::error("Stats", "❌ Не удалось создать ", historyFile.string());
        return;
    }
    historyOut << finalStats.dump(2);
    historyOut.close();

    // Удаляем tmp/stats
    fs::remove(statsFile, ec);

    Log::info("Stats", "✓ Статистика финализирована для uuid=", uuid,
              ": ", historyFile.string());
}
