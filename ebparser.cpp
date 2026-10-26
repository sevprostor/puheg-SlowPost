#include "ebparser.h"
#include "config.h"
//#include "transport.h"
#include "log.h"      // <-- единый вывод
#include "libs/json.hpp"
#include "words.h"
#include "file.h"
#include "process.h"
#include "routemanager.h"
#include "address.h"
#include "proof.h"
#include "stats.h"

#include <string>
#include <fstream>
#include <random>

using json = nlohmann::json;

//RunningProcess runningProc;

//File file;

void EBParser::parseEmsg(const EBMessage& msg, File& file) {
    if (!msg.evenbus) return;

    json j = json::parse(msg.rawtext, nullptr, false);
    if (j.is_discarded()) return;

    //Log::info("EventBus", msg.rawtext);

    // В сообщениях приходят статусы адаптера BUSY||READY
    if(j.contains("drv_status")){
        std::string status = j["drv_status"].get<std::string>();

        if(status == "BUSY"){
            config.driverState.busy = true;

            // Засечь время, чтобы если что - отвиснуть по таймату.
            // Таймаут проверяется в main

            config.driverState.busySince = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

        }
        if(status == "READY"){
            config.driverState.busy = false;
        }
    }

    if (j.contains("netprofile")) netprofileTopic(msg, file);
    if (j.contains("words")) wordsTopic(msg, file);
    if (j.contains("process")) processTopic(msg, file);
    if (j.contains("transport")) transportTopic(msg, file);

}


void EBParser::transportTopic(const EBMessage& msg, File& file) {


    //static uint32_t lastId = 0;
    //static std::chrono::steady_clock::time_point lastStart;


    json j = json::parse(msg.rawtext, nullptr, false);
    if (j.is_discarded()) return;

    if (!j.contains("transport") || !j["transport"].is_object()) return;

    const auto& tr = j["transport"];

    // Извлекаем основные поля
    uint32_t id = tr.value("id", (uint32_t)0);



    uint64_t parent = 0;
    if (tr.contains("parent")) {
        if (tr["parent"].is_number()) {
            parent = tr["parent"].get<uint64_t>();
        } else if (tr["parent"].is_string()) {
            try { parent = std::stoull(tr["parent"].get<std::string>()); } catch (...) {}
        }
    }

    uint16_t pair = static_cast<uint16_t>(tr.value("pair", 0));
    std::string state = tr.value("state", std::string("UNKNOWN"));
    uint8_t speed = tr.value("speed", 0);

    // Извлекаем проценты из uplink или downlink
    int currentPart = 0;
    int totalParts = 0;
    uint8_t tx = true;

    if (tr.contains("uplink") && tr["uplink"].is_object()) {
        const auto& uplink = tr["uplink"];
        totalParts = uplink.value("total", 0);
        if (uplink.contains("TX") && uplink["TX"].is_object()) {
            currentPart = uplink["TX"].value("part", 0);
            tx = true;
        }
    } else if (tr.contains("uplink") && tr["uplink"].is_object()) {
        const auto& uplink = tr["uplink"];
        //totalParts = uplink.value("total", 0);
        if (uplink.contains("RX") && uplink["RX"].is_object()) {
            //currentPart = uplink["TX"].value("part", 0);
            tx = false;
        }
    } else if (tr.contains("downlink") && tr["downlink"].is_object()) {
        const auto& downlink = tr["downlink"];
        totalParts = downlink.value("total", 0);
        if (downlink.contains("RX") && downlink["RX"].is_object()) {
            currentPart = downlink["RX"].value("part", 0);
            tx = false;
        }
    } else if (tr.contains("downlink") && tr["downlink"].is_object()) {
        const auto& downlink = tr["downlink"];
        //totalParts = downlink.value("total", 0);
        if (downlink.contains("TX") && downlink["TX"].is_object()) {
            //currentPart = downlink["RX"].value("part", 0);
            tx = true;
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////
    ///
    /// Здесь будет происходить сбор статистики. Если в этом транспорте появился words,
    /// то все учтенные прежде данные относятся к появившемуся чанку.
    ///

    // Если это какой-то новый транспорт, то создается объект новой записи,
    // в котором сохраняются самые первые параметры. Следующая итерация произойдет по окончании
    // трансмиссии.
    // - прием: данные впервые будут записаны в assembleFile /inbox/peerIP/uuid/tmp/stats.
    //   До конца приема всего файла он будет обновляться. В финале это все будет сохранено
    //   в inbox/stats/uuid.json, как это уже частично реализовано сейчас.
    // - передача: -пока не придумано-


    if(tr.contains("downlink")) Statistics::collectIncomingTransportStats(id, Address::macToIp(pair), speed);
    if(tr.contains("uplink")) Statistics::collectOutgoingTransportStats(id, Address::macToIp(pair), speed);

    /*
    if(lastId != id){

        //auto now = std::chrono::system_clock::now();
        //uint64_t config.nowEpoch() = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(now));

        // Началась новая передача

        lastId = id;
        //lastStart = std::chrono::steady_clock::now();
        FileStats newStats;

        if(tr.contains("downlink")){

            Log::debug("EBP", "-------- Downlink #", id, " --------");
            newStats.startTime = config.nowEpoch();
            newStats.lastHopIp = Address::macToIp(pair);
            Statistics::transmissionStats = newStats;

        }

        if(tr.contains("uplink")){

            Log::debug("EBP", "-------- Uplink #", id, " --------");
            newStats.startTime = config.nowEpoch();
            newStats.nextHopIp = Address::macToIp(pair);
            Statistics::transmissionStats = newStats;

        }


    }

    Statistics::transmissionStats.speedAbs = speed;
    */



    //////////////////////////////////////////
    /// Рендеринг прогресса
    /// Вычисляем процент

    int percent = 0;
    if (totalParts > 0) {
        percent = ((currentPart + 1) * 100) / totalParts;
    }

    // Находим IP пира по pair (ID)
    std::string peerIp = "unknown";
    for (const auto& contact : config.addressbook) {
        if (contact.mac == pair) {
            peerIp = contact.ip;
            break;
        }
    }

    // Логируем информацию
    if(tx)
        Log::tx("EBP", pair, ":", peerIp, " ",
              currentPart + 1, "/", totalParts, " (", percent, "%) ",
                "spd ", std::to_string(speed));
    else
        Log::rx("EBP", pair, ":", peerIp, " ",
                currentPart + 1, "/", totalParts, " (", percent, "%) ",
                "spd ", std::to_string(speed));

}

void EBParser::rxtxTopic(const EBMessage& msg) {

    json j = json::parse(msg.rawtext, nullptr, false);
    if (j.is_discarded()) return;

    Log::info("EventBus", "rxtxTopic: ", msg.rawtext);

    if (!j.contains("rx") || !j.contains("tx")) return;

    if (j.contains("rx") && j["rx"].is_object()){
        if(j["rx"]["state"] == "UP"){
            config.driverState.RX = true;
            config.driverState.busy = true;
            Log::info("EventBus", "Begin RX...");
        }
        if(j["rx"]["state"] == "DOWN"){
            config.driverState.RX = false;
            config.driverState.busy = false;
            Log::info("EventBus", "End RX.");
        }
    }

    if (j.contains("tx")){
        if(j["tx"]["state"] == "UP"){
            config.driverState.TX = true;
            config.driverState.busy = true;
            Log::info("EventBus", "Begin TX...");
        }
        if(j["tx"]["state"] == "DOWN"){
            config.driverState.TX = false;
            config.driverState.busy = false;
            Log::info("EventBus", "End TX...");
        }
    }


}

void EBParser::processTopic(const EBMessage& msg, File& file) {

    json j = json::parse(msg.rawtext, nullptr, false);
    if (j.is_discarded()) return;

    if (!j.contains("process") || !j["process"].is_object()) return;

    if (!j.contains("private")){
        Log::debug("EBP", "Это не для меня");
        return;
    }

    const auto& proc = j["process"];
    uint64_t thread = 0;
    if (proc.contains("thread")) {
        if (proc["thread"].is_number()) {
            thread = proc["thread"].get<uint64_t>();
        } else if (proc["thread"].is_string()) {
            thread = std::stoull(proc["thread"].get<std::string>());
        }
    }

    std::string state;
    if (proc.contains("state") && proc["state"].is_string()) {
        state = proc["state"].get<std::string>();
    }

    Log::info("EBParser", "📋 Process thread=", thread, ", state=", state);

    // Сохраняем состояние в глобальной переменной (будет использоваться в main)
    // Здесь же удалим файл, если это относится к отправке

    if(state == "OK"){}

    runningProc.thread = thread;
    runningProc.state = state;
}

void EBParser::netprofileTopic(const EBMessage& msg, File& file) {
    json j = json::parse(msg.rawtext, nullptr, false);
    if (j.is_discarded()) {
        Log::error("EBParser", "❌ Ошибка парсинга JSON");
        return;
    }

    if (!j.contains("netprofile") || !j["netprofile"].contains("contacts")) {
        Log::warn("EBParser", "⚠️ В JSON отсутствует netprofile.contacts");
        return;
    }

    const auto& np = j["netprofile"];
    const auto& contacts = np["contacts"];

    if (!contacts.is_array()) {
        Log::error("EBParser", "❌ netprofile.contacts не является массивом");
        return;
    }

    Log::info("EBParser", "📡 NetProfile получен, контактов: ", contacts.size());

    // Очищаем адресную книгу перед заполнением
    config.addressbook.clear();

    Config::abc contact;
    for (const auto& c : contacts) {
        uint16_t id = static_cast<uint16_t>(c.value("id", 0));
        std::string ip = c.value("ip", "");
        std::string key = c.value("key", "");

        contact.mac = id;
        contact.ip = ip;
        contact.key = key;

        // Проверяем флаг myOwn
        if (c.contains("myOwn")) {
            config.myContact = contact;
            Log::info("EBParser", "  👤 Мой контакт: id=", id, ", ip=", ip);
        } else {
            config.addressbook.emplace_back(contact);
            Log::info("EBParser", "  - id=", id, ", ip=", ip);
        }
    }

    Log::info("EBParser", "✅ Адресная книга загружена: ", config.addressbook.size(), " контактов");

    //Проверка, есть ли Routes и его создание с первыми записями, если нет
    RouteManager router;
    router.initRoutesFile();

}

void EBParser::wordsTopic(const EBMessage& msg, File& file){

    Words words;
    RouteManager router;
    std::error_code ec;

    //std::string topic = msg.rawtext;
    json j = json::parse(msg.rawtext, nullptr, false);

    std::vector<uint8_t> bytes = Words::hexToBytes(j["words"].value("payload", std::string("")));



    if (bytes.size() < 3 || bytes[0] != 'P' || bytes[1] != 'W' || bytes[2] != 0x01) {
        Log::warn("ListenWords", "⚠️ Отсутствует магик PW\\x01");
        return;
    }


    std::vector<uint8_t> body(bytes.begin() + 3, bytes.end());

    Envelope env;

    //сразу записать, от кого это пришло здесь и сейчас
    env.lasthop = j["words"].value("lasthop", 0);

    //разобрать конверт
    size_t envSize = words.parseEnv(body, env);

    if (envSize < 1) return;

    std::string routestr;

    for(int i = 0; i < env.route.size(); i++){

        routestr += std::to_string(env.route[i]);
        if(i < env.route.size() - 1) routestr += "->";

    }


    Log::rx("EBP", "Получен пакет типа '", char(env.type), "', envelope ", envSize, "B");
    Log::info("EBP", "маршрут [", routestr, "] ",
              std::to_string(env.lasthop), "->(Me)->", std::to_string(env.nexthop), ", получатель",
              std::to_string(env.fdest));


    // Если некстхопа нет, то это для меня, пробуем прочитать содержимое
    if(env.nexthop == 0){
        Log::info("EBP", "Я адресат, разбираю содержимое...");

        /////////////////////////////////////////////////////////
        /// Именно здесь нужно сохранить маршрут в /tmp/route,
        /// если файла еще нет.
        /////////////////////////////////////////////////////////

        switch (env.type) {
        case 'F': {
            PwFile pwFile;
            pwFile.env = env; // копируем envelope (с uuid из конверта)

            // туда передать body и размер envelope (откуда начинается сам файл)
            if (words.parseFile(body, envSize, pwFile)) {


                bool dublicate = false;

                // Кейсы дедубликации чанков
                // 1. Передача только что закончилась, пруф ушел в спул, еще не отправился,
                // в инбоксе пока висит финишированная передача:

                dublicate = (file.isInboxJustFinished(env.uuid)); // (разные || методы || проверки)

                if(dublicate){

                    Log::warn("EBP", "Входящая передача ", env.uuid, " уже завершена, это дубикат. Игнор.");
                    return;

                }


                Log::info("EBP", "📥 FILE: ", pwFile.name,
                          " part ", std::to_string(pwFile.part), "/", std::to_string(pwFile.totalParts),
                          " uuid=", std::to_string(pwFile.uuid),
                          " (env.uuid=", std::to_string(env.uuid), ")",
                          " origin=", std::to_string(pwFile.origin),
                          " content=", pwFile.content.size(), " байт");

                // Сохранить маршрут из входящего пакета (используем uuid из нагрузки — надёжный)
                router.saveIncomingRoute(pwFile.origin, env.route, pwFile.uuid);

                // Сохранить статистику
                Statistics::updateIncomingStats(pwFile);

                // Сборка файла
                if (file.assembleFile(pwFile)) {
                    Log::success("EBP", "Файл полностью получен");
                }
            }
            break;
        }

        ////////////////////////////////////////////////////////////////
        ///
        /// Обработка финального и промежуточного пруфа на стороне
        /// получателя пруфа пока одинакова
        ///

        case 'P': [[fallthrough]];
        case 'p': {
            //Log::info("EBParser", "📥 PROOF: uuid=", env.uuid, " (из конверта)");

            // Читаем uuid из нагрузки (надёжный, защищённый от изменений)
            if (envSize + 4 > body.size()) {
                Log::error("EBParser", "❌ Пруф слишком короткий: ", body.size(), " байт");
                break;
            }

            uint32_t payloadUuid = static_cast<uint32_t>(
                (body[envSize] << 24) | (body[envSize + 1] << 16) |
                (body[envSize + 2] << 8) | body[envSize + 3]);

            //Log::info("EBParser", "   payloadUuid=", payloadUuid,
            //          " (совпадает с env.uuid: ", (payloadUuid == env.uuid ? "да" : "НЕТ"), ")");

            Log::info("EBP", "Промежуточный 'p' пруф uuid ", payloadUuid, " (env.uuid=", env.uuid, ")");

            // Content начинается после uuid в нагрузке
            std::vector<uint8_t> approvedParts(body.begin() + envSize + 4, body.end());

            Log::info("EBP", "   подтверждено частей: ", approvedParts.size());

            // Используем uuid из нагрузки — это авторитетный источник
            if (Proof::processIncomingProof(payloadUuid, approvedParts)) {
                Log::debug("EBP", "Пруф обработан успешно (uuid=", payloadUuid, ")");
            } else {
                Log::warn("EBP", "Передача uuid ", payloadUuid, " не найдена, игнорируем");
            }
            break;
        }

        case 'C': {
            PwCommand cmd;
            if (words.parseCommand(body, cmd)) {
                //Log::info("ListenWords", "⚡ COMMAND от sender=", cmd.env.sender);
            }
            break;
        }

        default:
            Log::warn("EBParser", "⚠️ Неизвестный тип пакета: '", env.type, "'");
        }

    } else {

        Log::info("EBParser", "Я не адресат, нужно переложить это в отправку!");

        ///////////////////////////////////////////////////////////////
        ///
        /// ФИЛЬТРАЦИЯ ПРУФОВ
        /// - Если это промежуточный пруф типа 'p' и у нас есть неотправленные чанки
        ///   этой передачи — отбрасываем его, не ретранслируем.
        /// - Если это финальный 'P' пруф и у меня есть неотправленные чанки передачи,
        ///   то чанки скинуть, а пруф немедленно ретранслировать.
        ///


        if (env.type == 'P' || env.type == 'p') {


            fs::path spoolDir = fs::path(config.workDir) /
                                (config.myContact.ip.empty() ? "unknown" : config.myContact.ip) /
                                "outbox" / "spool";

            std::error_code ec;
            //bool hasChunks = false;
            //std::string uuidStr = std::to_string(env.uuid); // используем uuid из конверта

            //Log::info("EBP", "Это пруф на передачу ", env.uuid);


            // Проверяем spool/forward/
            fs::path forwardDir = spoolDir / "forward";

            /*
            if (fs::exists(forwardDir, ec)) {
                for (const auto& entry : fs::directory_iterator(forwardDir, ec)) {
                    if (!entry.is_regular_file()) continue;

                    std::string name = entry.path().filename().string();

                    //У каждого pwp файла нужно просмотреть envelope и взять uuid оттуда



                    Envelope fwdEnv;

                    //Нужно прочитать содержимое entry и обработать в words.parseEnv

                }
            }

            // Проверяем корень spool/
            if (!hasChunks && fs::exists(spoolDir, ec)) {
                for (const auto& entry : fs::directory_iterator(spoolDir, ec)) {
                    if (!entry.is_regular_file()) continue;
                    std::string name = entry.path().filename().string();
                    if (name.find(uuidStr) != std::string::npos) {
                        hasChunks = true;
                        Log::info("EBParser", "   найден чанк в spool/: ", name);
                        break;
                    }
                }
            }

            if (hasChunks) {
                Log::info("EBParser", "🚫 Отбрасываем пруф uuid=", env.uuid,
                          " — у нас есть неотправленные чанки этой передачи");
                return; // Не ретранслируем
            }
            */

            // Проверяем наличие неотправленных чанков этой передачи
            bool hasChunks = hasPwpWithUuid(spoolDir / "forward", env.uuid) ||
                             hasPwpWithUuid(spoolDir, env.uuid);

            switch(env.type){

                // Промежуточный пруф не одолевает чанк
                case 'p':{

                    if(hasChunks){
                        Log::info("EBP", "Прромежуточный 'p' пруф uuid=", env.uuid,
                                  " отброшен, т.к. у меня есть чанк передачи");
                        return; // Не ретранслируем
                    }

                    Log::info("EBP", "Прромежуточный 'p' пруф uuid=", env.uuid, ", ретрансляция");

                    break;
                }

                // Финальный пруф разрушает чанки подтверждаемой передачи на своем пути
                case 'P': {

                    // Если маршрут устарел — можно пересчитать его через локальные таблицы
                    // RouteManager router;
                    // std::vector<uint16_t> newRoute = router.findRouteTo(env.route.back());
                    // Но пока оставляем маршрут как есть, транзитёры его не меняют

                    int removedTotal = 0;

                    // Удаляем чанки из forward/
                    removedTotal += file.removePwpWithUuid(spoolDir / "forward", env.uuid);

                    // Удаляем чанки из spool/
                    removedTotal += file.removePwpWithUuid(spoolDir, env.uuid);

                    if (removedTotal > 0) {
                        Log::info("EBP", "🔥 Финальный 'P' пруф uuid=", env.uuid,
                                  " — удалено ", removedTotal, " чанков передачи");
                    } else {
                        Log::info("EBP", "Финальный 'P' пруф uuid=", env.uuid,
                                  " — у меня нет чанков этой передачи");
                    }

                    // Финальный пруф всегда ретранслируется дальше,
                    // даже если у нас не было чанков
                    Log::info("EBP", "Финальный 'P' пруф ретранслирую дальше");
                    break;
                }
            }
        }

        // ============================================================
        // РЕТРАНСЛЯЦИЯ
        // ============================================================

        // IP следующего хопа для имени файла
        std::string nextHopIp = Address::macToIp(env.nexthop);

        // Тело пакета без старого конверта
        std::vector<uint8_t> payload(body.begin() + envSize, body.end());

        // Новый конверт с тем же маршрутом и тем же uuid
        std::vector<uint8_t> newEnv = Words::buildEnvelope(env.route, env.uuid, env.type);

        // Склеиваем: новый конверт + тело
        std::vector<uint8_t> packet;
        packet.reserve(newEnv.size() + payload.size());
        packet.insert(packet.end(), newEnv.begin(), newEnv.end());
        packet.insert(packet.end(), payload.begin(), payload.end());

        // Сохраняем в spool с именем, содержащим nextHop
        fs::path spoolDir = fs::path(config.workDir) /
                            (config.myContact.ip.empty() ? "unknown" : config.myContact.ip) /
                            "outbox" / "spool";

        std::error_code ec;

        // Генерируем случайный UUID для имени .pwp (это uuid файла на диске, не пакета)
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist;
        uint32_t pwpUuid = dist(gen);

        // Имя: 0-1-0-<nextHopIp>-<pwpUuid>.pwp
        std::string pwpName = "0-1-0-" + nextHopIp + "-" + std::to_string(pwpUuid) + ".pwp";

        // Выбираем путь в зависимости от размера пакета
        fs::path targetDir;
        if (packet.size() < 100) {
            // Маленькие пакеты (например, пруфы) кладём прямо в spoolDir
            targetDir = spoolDir;
            fs::create_directories(spoolDir, ec);
            Log::info("EBP", "📦 Малый пакет (", packet.size(), " байт) → spoolDir");
        } else {
            // Большие пакеты — в spoolDir/forward/
            targetDir = spoolDir / "forward";
            fs::create_directories(targetDir, ec);
            Log::info("EBP", "📦 Большой пакет (", packet.size(), " байт) → forward/");
        }

        fs::path pwpPath = targetDir / pwpName;

        std::ofstream out(pwpPath.string(), std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(reinterpret_cast<const char*>(packet.data()),
                      static_cast<std::streamsize>(packet.size()));
            out.close();
            Log::info("EBP", "📦 Пакет для ретрансляции сохранён: ", pwpPath.string());
        } else {
            Log::error("EBP", "❌ Не удалось сохранить пакет для ретрансляции");
        }
    }

}

bool EBParser::hasPwpWithUuid(const fs::path& dir, uint32_t targetUuid) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return false;

    Words words;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".pwp") continue;

        std::ifstream pwpFile(entry.path().string(), std::ios::binary);
        if (!pwpFile) continue;

        std::vector<uint8_t> pwpContent((std::istreambuf_iterator<char>(pwpFile)),
                                        std::istreambuf_iterator<char>());
        pwpFile.close();

        // Проверяем магик PW\x01
        if (pwpContent.size() < 3) continue;
        if (pwpContent[0] != 'P' || pwpContent[1] != 'W' || pwpContent[2] != 0x01) continue;

        // Пропускаем магик (3 байта)
        std::vector<uint8_t> body(pwpContent.begin() + 3, pwpContent.end());

        // Парсим envelope
        Envelope env;
        size_t envSize = words.parseEnv(body, env);

        // Проверяем UUID И тип (только чанки 'F', не пруфы 'P')
        if (envSize > 0 && env.uuid == targetUuid && env.type == 'F') {
            return true;
        }
    }

    return false;
}
