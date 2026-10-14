#include "routemanager.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <set>
#include <iostream>

#include "file.h"
#include "log.h"
#include "config.h"
#include "address.h"


bool RouteManager::initRoutesFile() {

    fs::path userDir = fs::path(config.workDir) / fs::path(config.myContact.ip);
    fs::path routesPath = userDir / "routes";
    File file;

    file.ensureDir(userDir);

    std::error_code ec;
    if (fs::exists(routesPath, ec)) {
        return true;  // файл уже есть, не трогаем
    }

    std::ofstream out(routesPath.string(), std::ios::trunc);
    if (!out) {
        Log::error("Route", "Не удалось создать файл маршрутов");
        return false;
    }

    uint64_t now = currentTimestamp();
    int count = 0;

    // Для каждого контакта из адресной книги — 1-хоповый маршрут
    for (const auto& contact : config.addressbook) {
        if (contact.mac == config.myContact.mac) continue;  // себя пропускаем

        out << now << " " << contact.mac << " 0/0\n";
        now++;  // время должно быть уникальным
        count++;
    }

    out.close();
    Log::info("Route", "Создан файл маршрутов с ", count, " записями");
    return true;
}

bool RouteManager::hasLoop(const std::vector<uint16_t>& chain) {
    std::set<uint16_t> seen;
    for (uint16_t addr : chain) {
        if (seen.count(addr)) return true;
        seen.insert(addr);
    }
    return false;
}

std::vector<uint16_t> RouteManager::findRouteTo(uint16_t dest) {

    //Поиск лучшего пути в файле маршрутов

    fs::path routesPath = fs::path(config.workDir) / fs::path(config.myContact.ip) / "routes";
    std::ifstream in(routesPath.string());
    if (!in) {
        Log::warn("Route", "Файл маршрутов не найден");
        return {};
    }

    /*
    struct Candidate {
        std::vector<uint16_t> path;
        int attempts;
        int successes;

        double rate() const {
            if (attempts == 0) return 0.0;
            return static_cast<double>(successes) / attempts;
        }
    };
    */

    //std::vector<Candidate> candidates;
    std::vector<RouteRecord> candidates;
    std::string line;

    //Полинейное чтение файла маршрутов.
    while (std::getline(in, line)) {
        RouteRecord rec;
        if (!parseRouteLine(line, rec)) continue;

        // Это бесполезно, убрать, см. ниже
        /*if (hasLoop(rec.chain)) {
            Log::warn("Route", "Маршрут с петлёй пропущен");
            continue;
        }*/

        // Ищем первое вхождение получателя
        auto it = std::find(rec.chain.begin(), rec.chain.end(), dest);
        if (it == rec.chain.end()) continue;

        // Берём подцепочку от начала до получателя включительно
        std::vector<uint16_t> path(rec.chain.begin(), it + 1);

        // Позже - Проверить на петли и сразу их вырезать
        //if (hasLoop(path)) continue;

        //Candidate cand;
        RouteRecord cand;
        //cand.path = path;
        cand.chain = path;
        cand.attempts = rec.attempts;
        cand.successes = rec.successes;
        candidates.push_back(cand);
    }

    if (candidates.empty()) {
        Log::warn("Route", "Маршрут до ", dest, " не найден, связь прямая");
        return {};
    }

    // Сортировка:
    // 1. Доля успеха (по убыванию)
    // 2. Количество попыток (по убыванию)
    // 3. Длина маршрута (по возрастанию)
    std::sort(candidates.begin(), candidates.end(),
              //[](const Candidate& a, const Candidate& b) {
                [](const RouteRecord& a, const RouteRecord& b) {
                  if (a.rate() != b.rate()) return a.rate() > b.rate();
                  if (a.attempts != b.attempts) return a.attempts > b.attempts;
                  //return a.path.size() < b.path.size();
                  return a.chain.size() < b.chain.size();
              });

    const auto& best = candidates.front();

    std::string logroute;
    //for(Candidate cnd : candidates){
    for(RouteRecord cnd : candidates){

        logroute = "";

        //for (uint16_t bpth : cnd.path){
        for (uint16_t bpth : cnd.chain){

            logroute += "->";
            logroute += std::to_string(bpth);

        }

        Log::info("Route", "просмотрен маршрут ", logroute, ": ",
                  cnd.chain.size(), " хопов, качество ",
                  cnd.successes, "/", cnd.attempts);

    }

    logroute = "";
    for (uint16_t bpth : best.chain){

        logroute += "->";
        logroute += std::to_string(bpth);

    }

    Log::info("Route", "Выбран маршрут ", logroute);

    return best.chain;
}


std::vector<uint16_t> RouteManager::getRouteForTransfer(uint16_t destMac, uint32_t uuid) {

    File file;

    // Путь: spool/<peerIp>/tmp/route
    fs::path tmpDir = file.getSpoolBase() / Address::macToIp(destMac) / std::to_string(uuid) / "tmp";
    //std::error_code ec;

    fs::path routeFile = tmpDir / "route";

    std::ifstream in(routeFile.string());
    if (!in) {
        Log::error("Route", "Файл маршрутов не найден");
        return {};
    }

    RouteRecord trnsRoute;
    std::string line;

    std::getline(in, line);

    //Если в инбоксе в uuid нет ничего кроме tmp/route - удалить uuid
    // Проверяем, остался ли в каталоге uuid только tmp/route
    /*
    fs::path uuidDir = tmpDir.parent_path();
    std::error_code ec;

    if (fs::exists(uuidDir, ec)) {
        bool onlyTmpRoute = true;

        for (const auto& entry : fs::directory_iterator(uuidDir, ec)) {
            if (entry.path() == tmpDir) continue;  // пропускаем tmp/

            // Нашли что-то ещё — каталог не пуст
            onlyTmpRoute = false;
            break;
        }

        if (onlyTmpRoute) {
            fs::remove_all(uuidDir, ec);
            if (!ec) {
                Log::info("Route", "✓ Удалён пустой каталог uuid: ", uuidDir.string());
            }
        }
    }
    */

    if (!parseRouteLine(line, trnsRoute)){
        Log::error("Route", "Битый маршрут!");
        return {};
    }

    return trnsRoute.chain;

}

void RouteManager::saveIncomingRoute(uint32_t origin, std::vector<uint16_t> route, uint32_t uuid) {

    if (route.empty()) {
        Log::warn("Route", "Пустой маршрут, нечего сохранять");
        return;
    }

    // 1. Если первый узел != origin — добавить origin первым
    //uint16_t originMac = static_cast<uint16_t>(origin);
    if (route.front() != origin) {
        route.insert(route.begin(), origin);
    }

    // 2. Удалить последний пункт маршрута (себя)
    if (route.back() == config.myContact.mac) {
        route.pop_back();
    }

    if (route.empty()) {
        Log::warn("Route", "После удаления себя маршрут стал пустым");
        return;
    }



    // 3. Развернуть маршрут задом-наперёд
    std::reverse(route.begin(), route.end());

    // 4. Найти рабочую папку входящего файла
    // Структура: inbox/<senderIp>/<uuid>/
    std::string senderIp = Address::macToIp(origin);

    fs::path inboxBase = config.workDir;
    inboxBase /= config.myContact.ip.empty() ? "unknown" : config.myContact.ip;
    inboxBase /= "inbox";
    inboxBase /= senderIp;
    inboxBase /= std::to_string(uuid);

    std::error_code ec;
    File file;
    file.ensureDir(inboxBase);

    /*

    if (!fs::exists(inboxBase, ec)) {
        Log::warn("Route", "Каталог входящей передачи не найден: ", inboxBase.string());
        return;
    }
    */

    // Создаём tmp/ и сохраняем route
    fs::path tmpDir = inboxBase / "tmp";
    fs::create_directories(tmpDir, ec);

    fs::path routeFile = tmpDir / "route";

    // Время должно быть строго больше максимального в общем файле
    uint64_t now = currentTimestamp();
    uint64_t lastTime = getLastTimestampFromRoutes();
    if (now <= lastTime) {
        now = lastTime + 1;
    }

    std::ofstream out(routeFile.string(), std::ios::trunc);
    if (!out) {
        Log::error("Route", "Не удалось создать файл маршрута: ", routeFile.string());
        return;
    }

    out << now << " ";
    for (size_t i = 0; i < route.size(); ++i) {
        if (i > 0) out << ",";
        out << route[i];
    }
    out << " 1/1\n";
    out.close();

    Log::info("Route", "Обратный маршрут для uuid=", uuid,
              " сохранён в ", routeFile.string());
}

std::vector<uint16_t> RouteManager::getRouteForReply(uint32_t uuid) {

    // Базовый путь: <workDir>/<myIp>/inbox/
    fs::path inboxBase = config.workDir;
    inboxBase /= config.myContact.ip.empty() ? "unknown" : config.myContact.ip;
    inboxBase /= "inbox";

    std::error_code ec;
    if (!fs::exists(inboxBase, ec)) {
        Log::warn("Route", "Каталог inbox/ не найден");
        return {};
    }

    std::string uuidStr = std::to_string(uuid);

    // Обходим всех отправителей в inbox/
    for (const auto& senderEntry : fs::directory_iterator(inboxBase, ec)) {
        if (!senderEntry.is_directory()) continue;

        fs::path routeFile = senderEntry.path() / uuidStr / "tmp" / "route";

        if (!fs::exists(routeFile, ec)) continue;

        // Нашли файл — читаем
        std::ifstream in(routeFile.string());
        if (!in) {
            Log::warn("Route", "Не удалось открыть файл маршрута: ", routeFile.string());
            continue;
        }

        std::string line;
        std::getline(in, line);
        in.close();

        RouteRecord rec;
        if (parseRouteLine(line, rec)) {
            Log::info("Route", "Найден маршрут для ответа uuid=", uuid,
                      ": ", rec.chain.size(), " узлов");
            return rec.chain;
        }

        Log::warn("Route", "Не удалось распарсить маршрут: ", line);
    }

    //Log::warn("Route", "Маршрут для ответа uuid=", uuid, " не найден");
    return {};
}


bool RouteManager::saveRouteForProof(uint16_t destMac, uint32_t uuid){

    File file;

    // Базовый путь: <workDir>/<myIp>/inbox/
    fs::path inboxBase = config.workDir;
    inboxBase /= config.myContact.ip.empty() ? "unknown" : config.myContact.ip;
    inboxBase /= "inbox";

    std::error_code ec;
    if (!fs::exists(inboxBase, ec)) {
        Log::warn("Route", "Каталог inbox/ не найден");
        return false;
    }

    std::string uuidStr = std::to_string(uuid);

    // Обходим всех отправителей в inbox/
    for (const auto& senderEntry : fs::directory_iterator(inboxBase, ec)) {
        if (!senderEntry.is_directory()) continue;

        fs::path routeFile = senderEntry.path() / uuidStr / "tmp" / "route";

        if (!fs::exists(routeFile, ec)) continue;

        // Нашли файл — копируем его в исходящие
        //std::ifstream in(routeFile.string());
        //if (!in) {
        //    Log::warn("Route", "Не удалось открыть файл маршрута: ", routeFile.string());
        //    continue;
        //}

        // Путь: spool/<peerIp>/tmp/route
        fs::path tmpDir = file.getSpoolBase() / Address::macToIp(destMac) / std::to_string(uuid) / "tmp";
        std::error_code ec;
        fs::create_directories(tmpDir, ec);

        fs::path newRouteFile = tmpDir / "route";

        fs::copy_file(routeFile, newRouteFile, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            Log::error("File", "❌ Не удалось скопировать в work/: ", ec.message());
            return 0;
        }

        Log::info("Route", "Скопирован маршрут для пруфа uuid", uuid, " ", routeFile.string(), "->", newRouteFile.string());

        return true;

    }


    return false;

}

bool RouteManager::saveRouteForTransfer(uint16_t destMac, uint32_t uuid) {



    std::vector<uint16_t> route = findRouteTo(destMac);
    File file;

    // Если маршрут пустой — значит связь прямая,
    // создаём маршрут из 1 пункта (сам получатель)
    if (route.empty()) {
        route.push_back(destMac);
    }

    // Путь: spool/<peerIp>/tmp/route
    fs::path tmpDir = file.getSpoolBase() / Address::macToIp(destMac) / std::to_string(uuid) / "tmp";
    std::error_code ec;
    fs::create_directories(tmpDir, ec);

    fs::path routeFile = tmpDir / "route";

    std::ofstream out(routeFile.string(), std::ios::trunc);
    if (!out) {
        Log::error("Route", "Не удалось создать файл маршрута");
        return false;
    }

    uint64_t now = currentTimestamp();
    //uint64_t lastTime = getLastTimestampFromRoutes();
    //if (now <= lastTime) {
    //    now = lastTime + 1;
    //}

    out << now << " ";
    for (size_t i = 0; i < route.size(); ++i) {
        if (i > 0) out << ",";
        out << route[i];
    }
    out << " 1/0\n";

    out.close();
    Log::info("Route", "Маршрут для передачи сохранён: ", routeFile.string());
    return true;
}

void RouteManager::incrementAttempts(const std::string& destIp) {
    updateRouteFile(destIp, false);
}

void RouteManager::incrementSuccesses(const std::string& destIp) {
    updateRouteFile(destIp, true);
}

void RouteManager::updateRouteFile(const std::string& destIp, bool success) {

    File file;
    fs::path routeFile = file.getSpoolBase() / destIp / "tmp" / "route";

    std::ifstream in(routeFile.string());
    if (!in) {
        Log::warn("Route", "Файл маршрута не найден для обновления");
        return;
    }

    std::string line;
    std::getline(in, line);
    in.close();

    RouteRecord rec;
    if (!parseRouteLine(line, rec)) return;

    if (success) {
        rec.successes++;
    } else {
        rec.attempts++;
    }

    // Перезаписываем файл
    std::ofstream out(routeFile.string(), std::ios::trunc);
    out << rec.timestamp << " ";
    for (size_t i = 0; i < rec.chain.size(); ++i) {
        if (i > 0) out << ",";
        out << rec.chain[i];
    }
    out << " " << rec.attempts << "/" << rec.successes << "\n";
    out.close();
}

void RouteManager::commitRoute(const std::string& destIp) {

    File file;
    fs::path routeFile = file.getSpoolBase() / destIp / "tmp" / "route";
    fs::path routesPath = fs::path(config.workDir) / "routes";

    std::ifstream in(routeFile.string());
    if (!in) return;

    std::string line;
    std::getline(in, line);
    in.close();

    RouteRecord rec;
    if (!parseRouteLine(line, rec)) return;

    // Время должно быть строго больше максимального в общем файле
    uint64_t lastTime = getLastTimestampFromRoutes();
    if (rec.timestamp <= lastTime) {
        rec.timestamp = lastTime + 1;
    }

    // Дописываем новой строкой в общий файл
    std::ofstream out(routesPath.string(), std::ios::app);
    out << rec.timestamp << " ";
    for (size_t i = 0; i < rec.chain.size(); ++i) {
        if (i > 0) out << ",";
        out << rec.chain[i];
    }
    out << " " << rec.attempts << "/" << rec.successes << "\n";
    out.close();

    Log::info("Route", "Маршрут добавлен в общий файл");
}

uint64_t RouteManager::currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    struct tm tmBuf;
    localtime_r(&t, &tmBuf);

    char buf[15];
    strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tmBuf);
    return std::stoull(buf);
}

uint64_t RouteManager::getLastTimestampFromRoutes() {
    fs::path routesPath = fs::path(config.workDir) / "routes";
    std::ifstream in(routesPath.string());
    if (!in) return 0;

    uint64_t maxTime = 0;
    std::string line;
    while (std::getline(in, line)) {
        RouteRecord rec;
        if (parseRouteLine(line, rec)) {
            if (rec.timestamp > maxTime) maxTime = rec.timestamp;
        }
    }
    return maxTime;
}

bool RouteManager::parseRouteLine(const std::string& line, RouteRecord& rec) {
    if (line.empty() || line[0] == '#') return false;

    size_t firstSpace = line.find(' ');
    size_t lastSpace = line.rfind(' ');
    if (firstSpace == std::string::npos || lastSpace == std::string::npos
        || firstSpace == lastSpace) {
        return false;
    }

    try {
        rec.timestamp = std::stoull(line.substr(0, firstSpace));
    } catch (...) {
        return false;
    }

    std::string routeStr = line.substr(firstSpace + 1, lastSpace - firstSpace - 1);
    rec.chain.clear();
    std::istringstream iss(routeStr);
    std::string token;
    while (std::getline(iss, token, ',')) {
        try {
            rec.chain.push_back(static_cast<uint16_t>(std::stoul(token)));
        } catch (...) {
            return false;
        }
    }
    if (rec.chain.empty()) return false;

    std::string statStr = line.substr(lastSpace + 1);
    size_t slash = statStr.find('/');
    if (slash == std::string::npos) return false;

    try {
        rec.attempts = std::stoi(statStr.substr(0, slash));
        rec.successes = std::stoi(statStr.substr(slash + 1));
    } catch (...) {
        return false;
    }

    return true;
}
