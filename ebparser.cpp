#include "ebparser.h"
#include "config.h"
//#include "transport.h"
#include "log.h"      // <-- единый вывод
#include "../libs/json.hpp"
#include "words.h"
#include "file.h"
#include "process.h"
#include <string>

using json = nlohmann::json;

//RunningProcess runningProc;

//File file;

void EBParser::parseEmsg(const EBMessage& msg, File& file) {
    if (!msg.evenbus) return;

    json j = json::parse(msg.rawtext, nullptr, false);
    if (j.is_discarded()) return;

    //Log::info("EventBus", msg.rawtext);

    if (j.contains("netprofile")) netprofileTopic(msg, file);
    if (j.contains("words")) wordsTopic(msg, file);
    if (j.contains("process")) processTopic(msg, file);
    if (j.contains("rx") || j.contains("tx")) rxtxTopic(msg);
    if (j.contains("transport")) transportTopic(msg, file);

}


void EBParser::transportTopic(const EBMessage& msg, File& file) {

    static uint32_t lastId = 0;
    static std::chrono::steady_clock::time_point lastStart;


    json j = json::parse(msg.rawtext, nullptr, false);
    if (j.is_discarded()) return;

    if (!j.contains("transport") || !j["transport"].is_object()) return;

    const auto& tr = j["transport"];

    // Извлекаем основные поля
    uint32_t id = tr.value("id", (uint32_t)0);

    if(lastId != id){
        lastId = id;
        lastStart = std::chrono::steady_clock::now();
    }

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
    int speed = tr.value("speed", 0);

    // Извлекаем проценты из uplink или downlink
    int currentPart = 0;
    int totalParts = 0;
    std::string direction = "";

    if (tr.contains("uplink") && tr["uplink"].is_object()) {
        const auto& uplink = tr["uplink"];
        totalParts = uplink.value("total", 0);
        if (uplink.contains("TX") && uplink["TX"].is_object()) {
            currentPart = uplink["TX"].value("part", 0);
            direction = "TX";
        }
    } else if (tr.contains("downlink") && tr["downlink"].is_object()) {
        const auto& downlink = tr["downlink"];
        totalParts = downlink.value("total", 0);
        if (downlink.contains("RX") && downlink["RX"].is_object()) {
            currentPart = downlink["RX"].value("part", 0);
            direction = "RX";
        }
    }

    // Вычисляем процент
    int percent = 0;
    if (totalParts > 0) {
        percent = (currentPart * 100) / totalParts;
    }

    // Находим IP пира по pair (ID)
    std::string peerIp = "unknown";
    for (const auto& contact : config.addressbook) {
        if (contact.mac == pair) {
            peerIp = contact.ip;
            break;
        }
    }

    //id = 0;
    // Вычисление реальной скорости
    uint32_t realBPS = 0;
    if (currentPart > 0) {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - lastStart).count();

        if (elapsedMs > 0) {
            // Переданные байты = чанков * размер чанка
            uint64_t bytesSent = static_cast<uint64_t>(currentPart) * (config.chunkSize / totalParts);

            // Байт в секунду: bytesSent * 1000 / ms
            realBPS = static_cast<uint32_t>((bytesSent * 1000) / elapsedMs);
        }
    }

    // Логируем информацию
    Log::info("EBParser", "📡 Transport: ", direction, " [", pair, ":", peerIp, "] ",
              currentPart + 1, "/", totalParts, " (", percent, "%) ",
              "speed=", speed, " abs, Bps=", realBPS, "B/s");

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
}

void EBParser::wordsTopic(const EBMessage& msg, File& file){

    Words words;

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


    Log::info("EBParser", "Получен пакет типа '", char(env.type), "', envelope ", envSize, "B");
    Log::info("EBParser", "маршрут ", routestr, ", получатель ", std::to_string(env.fdest), ", ",
              std::to_string(env.lasthop), "->(Me)->", std::to_string(env.nexthop));



    // Если я стою в конце маршрута, то значит это мне и надо разбирать файл
    //if(env.fdest == config.myContact.mac){

    // Если некстхопа нет, то это для меня, пробуем прочитать содержимое
    if(env.nexthop == 0){
        Log::info("EBParser", "Я адресат, разбираю содержимое...");

        switch (env.type) {
        case 'F': {
            PwFile pwFile;

            //туда передать body и размер envelope (откуда начинается сам файл)
            if (words.parseFile(body, envSize, pwFile)) {
                Log::info("EBParser", "📥 FILE: ", pwFile.name,
                          " part ", std::to_string(pwFile.part), "/", std::to_string(pwFile.totalParts),
                          " uuid=", std::to_string(pwFile.uuid),
                          " origin=", std::to_string(pwFile.origin),
                          " content=", pwFile.content.size(), " байт");

                // 1. Просмотреть inbox/<sender>/uuid - переделать на IP-адрес!

                // Сборка файла
                if (file.assembleFile(pwFile)) {
                    Log::info("EBParser", "✅ Файл полностью получен!");
                }
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
    }

}
