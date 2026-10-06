#include "file.h"
#include "config.h"
#include "log.h"
#include "process.h"
#include "proof.h"
#include <fstream>
#include <algorithm>
#include <random>
#include <set>

//RunningProcess runningProc;

File::File() {
    // Конструктор может инициализировать базовые пути, если нужно
}



fs::path File::getOutboxDir(const std::string& contactIp) const {
    // <workDir>/<myIp>/outbox/<contactIp>/
    fs::path base = config.workDir;
    base /= config.myContact.ip.empty() ? "unknown" : config.myContact.ip;
    base /= "outbox";
    base /= contactIp;
    return base;
}

fs::path File::getOutgoingDir(const std::string& contactIp, const std::string& filename) const {
    // <workDir>/<myIp>/outbox/<contactIp>/outgoing/<filename>/
    return getOutboxDir(contactIp) / "outgoing" / filename;
}

bool File::ensureDir(const fs::path& dir) const {
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        return fs::is_directory(dir, ec);
    }
    fs::create_directories(dir, ec);
    if (ec) {
        Log::error("File", "❌ Не удалось создать каталог ", dir.string(), ": ", ec.message());
        return false;
    }
    Log::info("File", "📁 Создан каталог: ", dir.string());
    return true;
}

fs::path File::getInboxDir(const std::string& senderIp) const {
    // <workDir>/<myIp>/inbox/<senderIp>/
    fs::path base = config.workDir;
    base /= config.myContact.ip.empty() ? "unknown" : config.myContact.ip;
    base /= "inbox";
    base /= senderIp;
    return base;
}

void File::proofAvailableChunks() {
    fs::path inboxBase = config.workDir;
    inboxBase /= config.myContact.ip.empty() ? "unknown" : config.myContact.ip;
    inboxBase /= "inbox";

    std::error_code ec;
    if (!fs::exists(inboxBase, ec)) return;

    uint64_t nowTime = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    //const uint64_t timeoutSeconds = 5 * 60; // 5 минут

    // Обходим inbox/<peerIp>/<uuid>/
    for (const auto& ipEntry : fs::directory_iterator(inboxBase, ec)) {
        if (!ipEntry.is_directory()) continue;

        std::string peerIp = ipEntry.path().filename().string();

        for (const auto& uuidEntry : fs::directory_iterator(ipEntry.path(), ec)) {
            if (!uuidEntry.is_directory()) continue;

            fs::path uuidDir = uuidEntry.path();
            fs::path waitingDir = uuidDir / "waiting";

            // Считаем файлы в parent/ (кроме waiting/)
            int parentCount = 0;
            std::vector<int> parentParts;

            for (const auto& entry : fs::directory_iterator(uuidDir, ec)) {
                if (entry.is_directory()) continue;
                if (!entry.is_regular_file()) continue;

                parentCount++;

                std::string fname = entry.path().filename().string();
                size_t dash = fname.find('-');
                if (dash != std::string::npos) {
                    try {
                        int p = std::stoi(fname.substr(0, dash));
                        parentParts.push_back(p);
                    } catch (...) {}
                }
            }

            if (parentCount == 0) continue; // Нет подтверждённых чанков

            // Считаем файлы в waiting/
            //int waitingCount = 0;
            //std::vector<int> waitingParts;
            //uint64_t oldestTime = UINT64_MAX;

            //uint64_t newestTime = 0;

            /*
            if (fs::exists(waitingDir, ec)) {

                for (const auto& entry : fs::directory_iterator(waitingDir, ec)) {
                    if (!entry.is_regular_file()) continue;

                    waitingCount++;

                    // Парсим номер части
                    std::string fname = entry.path().filename().string();
                    size_t dash = fname.find('-');
                    if (dash != std::string::npos) {
                        try {
                            int p = std::stoi(fname.substr(0, dash));
                            waitingParts.push_back(p);
                        } catch (...) {}
                    }

                    // Время создания
                    uint64_t t = fileTimeToEpoch(entry.path());
                    if (t < oldestTime) oldestTime = t;
                }
            }
            */

            // Считаем файлы в waiting/
            int waitingCount = 0;
            std::vector<int> waitingParts;
            uint64_t newestTime = 0;  // Ищем самый новый файл
            fs::path newestFile;

            if (fs::exists(waitingDir, ec)) {
                for (const auto& entry : fs::directory_iterator(waitingDir, ec)) {
                    if (!entry.is_regular_file()) continue;

                    waitingCount++;

                    // Парсим номер части
                    std::string fname = entry.path().filename().string();
                    size_t dash = fname.find('-');
                    if (dash != std::string::npos) {
                        try {
                            int p = std::stoi(fname.substr(0, dash));
                            waitingParts.push_back(p);
                        } catch (...) {}
                    }

                    // Время создания - ищем самое новое
                    uint64_t t = fileTimeToEpoch(entry.path());
                    if (t > newestTime){
                        newestTime = t;
                        newestFile = entry.path();
                    }
                }
            }

            if (waitingCount == 0) continue;

            // Проверяем таймаут от самого нового файла
            uint64_t ageSeconds = nowTime - newestTime;


            //if (ageSeconds >= timeoutSeconds) {
            if (ageSeconds >= config.sendProofDelay) {
                uint32_t uuid = 0;
                try {
                    uuid = std::stoul(uuidEntry.path().filename().string());
                } catch (...) {
                    continue;
                }

                Log::info("File", "⏱️ Таймаут для uuid=", uuid,
                          " от ", peerIp, " (", ageSeconds, "с)");

                // Собираем все части
                std::vector<int> allPartsVec;
                allPartsVec.insert(allPartsVec.end(), parentParts.begin(), parentParts.end());
                allPartsVec.insert(allPartsVec.end(), waitingParts.begin(), waitingParts.end());

                // Отправляем batch-пруф
                if (Proof::sendFileProof(peerIp, uuid, allPartsVec)) {
                    // === НОВОЕ: обновляем время только самого нового файла ===
                    if (!newestFile.empty()) {
                        auto now = fs::file_time_type::clock::now();
                        fs::last_write_time(newestFile, now, ec);
                        if (!ec) {
                            Log::info("File", "✓ Обновлено время для ",
                                      newestFile.filename().string());
                        }
                    }

                    // Перемещаем чанки из waiting/ в parent/
                    /*
                    for (const auto& entry : fs::directory_iterator(waitingDir, ec)) {
                        if (!entry.is_regular_file()) continue;
                        fs::rename(entry.path(), uuidDir / entry.path().filename(), ec);
                    }
                    Log::info("File", "✓ Чанки перемещены после таймаута для uuid=", uuid);
                    */
                }
            }
        }
    }
}

bool File::assembleFile(const PwFile& pwFile) {


    //  Реализовать по шагам.
    //  1.
    //      - если в inbox/<senderIp>/<uuid>/waiting - один принятый прямо сейчас файл
    //      и в inbox/<senderIp>/<uuid> пусто, то тут же отправить пруф.
    //          Создать proof.h/cpp, там метод-заготовку для генерации и отправки пруфа. Пруф будет содержать
    //          uuid, номер принятой части. Отправляться будет стандартным путем через spool.
    //      После успешной отправки пруфа чанк переместить в inbox/<senderIp>/<uuid>.
    //
    //      - если в inbox/<senderIp>/<uuid> 1 файл, а все остальные, включая пришедший, собрались
    //      в inbox/<senderIp>/<uuid>/waiting, то отправить пруф (uuid, массив байт - номера принятых частей частей).
    //      После успешной отправки перенести файлы в inbox/<senderIp>/<uuid>/
    //
    //      *** дальнейшее должно происходить в proofAvailableChunks() и вызываться из main.
    //      *** Это должен быть рекурсивный просмотр uuid и waiting и создание пруфов по таймауту
    //
    //      - если в inbox/<senderIp>/<uuid> один или более файлов, а со времени создания последнего файла
    //      в inbox/<senderIp>/<uuid>/waiting прошло больше 5 минут, то отправить пруф


    uint32_t uuid = pwFile.uuid;
    int part = pwFile.part;
    int totalParts = pwFile.totalParts;



    // Находим IP отправителя по ID
    std::string senderIp = "unknown";
    for (const auto& contact : config.addressbook) {
        if (contact.mac == pwFile.origin) {
            senderIp = contact.ip;
            break;
        }
    }

    if (senderIp == "unknown") {
        Log::warn("File", "⚠️ Отправитель ID=", pwFile.origin,
                  " не найден в адресной книге, используем 'unknown'");
    }


    //Если это файл "PF", а его uuid есть в списке активных передач - значит это пруф
    //Обработать специальным методом

    // === ОБРАБОТКА ПРУФА ===
    // Если имя файла "PF" — это пруф для нашей исходящей передачи
    if (pwFile.name == "PF") {
        Log::info("File", "📥 Входящий пруф: uuid=", uuid,
                  ", подтверждено частей: ", pwFile.content.size());

        // processIncomingProof сам найдёт передачу по uuid
        if (Proof::processIncomingProof(uuid, pwFile.content)) {
            Log::info("File", "✅ Пруф обработан успешно");
            return true;  // выходим, не продолжаем как обычный файл
        } else {
            Log::warn("File", "⚠️ Передача uuid=", uuid, " не найдена, игнорируем пруф");
            return false;
        }
    }


    // Путь к inbox для этого отправителя
    fs::path inboxDir = getInboxDir(senderIp);
    if (!ensureDir(inboxDir)) {
        Log::error("File", "❌ Не удалось создать inbox для sender=", senderIp);
        return false;
    }

    // Путь к подкаталогу uuid
    fs::path uuidDir = inboxDir / std::to_string(uuid);
    if (!ensureDir(uuidDir)) {
        Log::error("File", "❌ Не удалось создать каталог для uuid=", uuid);
        return false;
    }

    // Каталог waiting/ для временного хранения чанков
    fs::path waitingDir = uuidDir / "waiting";
    if (!ensureDir(waitingDir)) {
        Log::error("File", "❌ Не удалось создать каталог waiting/");
        return false;
    }

    // Сохраняем чанок в waiting/: <part>-<totalParts>-<filename>
    std::string chunkName = std::to_string(part) + "-" + std::to_string(totalParts) + "-" + pwFile.name;
    fs::path chunkPath = waitingDir / chunkName;

    std::ofstream out(chunkPath.string(), std::ios::binary | std::ios::trunc);
    if (!out) {
        Log::error("File", "❌ Не удалось сохранить чанок: ", chunkPath.string());
        return false;
    }
    out.write(reinterpret_cast<const char*>(pwFile.content.data()),
              static_cast<std::streamsize>(pwFile.content.size()));
    out.close();

    Log::info("File", "📥 Часть ", part + 1, "/", totalParts, " файла ", pwFile.name,
              " от ", senderIp, " сохранена в waiting/");

    std::error_code ec;

    // Подсчёт файлов в разных каталогах
    int waitingCount = 0;
    int parentCount = 0;
    std::vector<int> waitingParts;

    // Считаем файлы в waiting/
    for (const auto& entry : fs::directory_iterator(waitingDir, ec)) {
        if (!entry.is_regular_file()) continue;
        waitingCount++;

        // Парсим номер части из имени
        std::string fname = entry.path().filename().string();
        size_t dash = fname.find('-');
        if (dash != std::string::npos) {
            try {
                int p = std::stoi(fname.substr(0, dash));
                waitingParts.push_back(p);
            } catch (...) {}
        }
    }

    // Считаем файлы в родительском каталоге (кроме waiting/)
    for (const auto& entry : fs::directory_iterator(uuidDir, ec)) {
        if (entry.is_directory()) continue; // пропускаем waiting/
        if (!entry.is_regular_file()) continue;
        parentCount++;
    }



    //Log::info("File", "📊 Статус uuid=", uuid, ": waiting=", waitingCount,
    //          ", parent=", parentCount);

    // ========== ШАГ 1: Первый чанок передачи ==========
    // Если в waiting/ один файл (только что пришедший) и в parent/ пусто
    if (waitingCount == 1 && parentCount == 0) {




        Log::info("File", "🎯 Первый чанок передачи uuid=", uuid);

        // Отправляем пруф для этого чанка


        // Proof - это точно такой же F-пакет. В нем указано uuid идущей передачи,
        // а в имени файла - "PF". Содержимое файла - байты с номерами полученных частей.


        // Нужно сохранить файл "<uuid>.pwProof" в /outbox/peer_ip/, и как только он оттуда исчезнет -
        // переместить чанку в inbox/<uuid>/


        std::vector<int> parts;
        parts.emplace_back(part);

        /////////////////////////////////////////////////////////////////////////////////
        //пруф должен отправляться отправителю, запись о котором должна быть внутри файла,
        //а не так как сейчас

        if (Proof::sendFileProof(senderIp, uuid, parts)) {
            // Успешно — перемещаем чанок из waiting/ в parent/

            fs::rename(chunkPath, uuidDir / chunkName, ec);
            if (ec) {
                Log::warn("File", "⚠️ Не удалось переместить чанок из waiting/");
            } else {
                Log::info("File", "✓ Чанок перемещён в inbox/<uuid>/");
            }
        }

        //Если частей больше одной, то выйти. Иначе функция будет продолжена и файл обработается как полностью полученный
        if(totalParts > 1) return false;
        else parentCount = 1;

    }

    // ========== ШАГ 2: Все чанки собраны ==========
    // Если в parent/ один файл и все остальные в waiting/
    if (parentCount == 1 && waitingCount > 0) {

        // Проверяем, все ли чанки на месте
        std::set<int> allParts;

        // Чанки из parent/
        for (const auto& entry : fs::directory_iterator(uuidDir, ec)) {
            if (entry.is_directory()) continue;
            if (!entry.is_regular_file()) continue;

            std::string fname = entry.path().filename().string();
            size_t dash = fname.find('-');
            if (dash != std::string::npos) {
                try {
                    int p = std::stoi(fname.substr(0, dash));
                    allParts.insert(p);
                } catch (...) {}
            }
        }

        // Чанки из waiting/
        for (int p : waitingParts) {
            allParts.insert(p);
        }

        // Проверяем, есть ли все части от 0 до totalParts-1
        bool allPresent = true;

        for (int i = 0; i < totalParts; ++i) {
            if (allParts.find(i) == allParts.end()) {
                allPresent = false;
                break;
            }
        }

        if (allPresent) {
            Log::info("File", "✅ Все ", totalParts, " чанков получены для uuid=", uuid);

            // Отправляем batch-пруф
            std::vector<int> allPartsVec(allParts.begin(), allParts.end());

            Proof::sendFileProof(senderIp, uuid, allPartsVec);


            for (const auto& entry : fs::directory_iterator(waitingDir, ec)) {
                if (!entry.is_regular_file()) continue;
                fs::rename(entry.path(), uuidDir / entry.path().filename(), ec);
            }
            Log::info("File", "✓ Все чанки перемещены в inbox/<uuid>/");



            // === СБОРКА ФИНАЛЬНОГО ФАЙЛА ===
            std::vector<uint8_t> completeData;
            std::string finalFilename;

            for (int i = 0; i < totalParts; ++i) {
                // Ищем чанок с нужным номером части в approved/
                fs::path chunkFile;
                for (const auto& entry : fs::directory_iterator(uuidDir, ec)) {
                    if (!entry.is_regular_file()) continue;
                    std::string fname = entry.path().filename().string();
                    size_t dash = fname.find('-');
                    if (dash != std::string::npos) {
                        try {
                            int p = std::stoi(fname.substr(0, dash));
                            if (p == i) {
                                chunkFile = entry.path();
                                if (finalFilename.empty()) {
                                    // Извлекаем имя файла: <part>-<total>-<filename>
                                    size_t d2 = fname.find('-', dash + 1);
                                    if (d2 != std::string::npos) {
                                        finalFilename = fname.substr(d2 + 1);
                                    }
                                }
                                break;
                            }
                        } catch (...) { continue; }
                    }
                }

                if (chunkFile.empty()) {
                    Log::error("File", "❌ Не найден чанок ", i);
                    return false;
                }

                std::ifstream chunkIn(chunkFile.string(), std::ios::binary | std::ios::ate);
                if (!chunkIn) continue;

                std::streamsize chunkSize = chunkIn.tellg();
                chunkIn.seekg(0, std::ios::beg);

                std::vector<char> chunkData(chunkSize);
                if (!chunkIn.read(chunkData.data(), chunkSize)) continue;
                chunkIn.close();

                completeData.insert(completeData.end(), chunkData.begin(), chunkData.end());
            }

            if (finalFilename.empty()) {
                Log::error("File", "❌ Не удалось определить имя файла");
                return false;
            }

            // Сохраняем финальный файл в inbox/<senderIp>/
            fs::path finalPath = inboxDir / finalFilename;
            std::ofstream finalOut(finalPath.string(), std::ios::binary | std::ios::trunc);
            if (!finalOut) {
                Log::error("File", "❌ Не удалось создать файл: ", finalPath.string());
                return false;
            }
            finalOut.write(reinterpret_cast<const char*>(completeData.data()),
                           static_cast<std::streamsize>(completeData.size()));
            finalOut.close();

            Log::info("File", "✅ Файл ", finalFilename, " собран в ",
                      finalPath.string(), " (", completeData.size(), " байт)");

            // Удаляем каталог uuid
            fs::remove_all(uuidDir, ec);
            if (ec) {
                Log::warn("File", "⚠️ Не удалось удалить каталог uuid: ", ec.message());
            } else {
                Log::info("File", "✓ Каталог uuid удалён: ", uuidDir.string());
            }

            //=== Конец сборки файла ===


        }
        return true;
    }

    // ========== ШАГ 3: Таймаут ожидания ==========
    // Если в parent/ есть файлы и прошло больше 5 минут с последнего чанка в waiting/
    // ВОТ ЭТО ВСЕ должно быть отдельным методом
    /*
    if (parentCount > 0 && waitingCount > 0) {
        // Находим самое старое время создания файла в waiting/
        uint64_t oldestTime = UINT64_MAX;
        for (const auto& entry : fs::directory_iterator(waitingDir, ec)) {
            if (!entry.is_regular_file()) continue;
            uint64_t t = fileTimeToEpoch(entry.path());
            if (t < oldestTime) oldestTime = t;
        }

        uint64_t nowTime = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        uint64_t ageSeconds = nowTime - oldestTime;
        const uint64_t timeoutSeconds = 5 * 60; // 5 минут

        if (ageSeconds >= timeoutSeconds) {
            Log::info("File", "⏱️ Таймаут: прошло ", ageSeconds, "с с последнего чанка");

            // Отправляем batch-пруф для всех полученных чанков
            std::vector<int> allPartsVec;

            // Чанки из parent/
            for (const auto& entry : fs::directory_iterator(uuidDir, ec)) {
                if (entry.is_directory()) continue;
                if (!entry.is_regular_file()) continue;

                std::string fname = entry.path().filename().string();
                size_t dash = fname.find('-');
                if (dash != std::string::npos) {
                    try {
                        int p = std::stoi(fname.substr(0, dash));
                        allPartsVec.push_back(p);
                    } catch (...) {}
                }
            }

            // Чанки из waiting/
            for (int p : waitingParts) {
                allPartsVec.push_back(p);
            }

            if (Proof::sendFileProof(senderIp, uuid, allPartsVec)) {
                // Перемещаем чанки из waiting/ в parent/
                for (const auto& entry : fs::directory_iterator(waitingDir, ec)) {
                    if (!entry.is_regular_file()) continue;
                    fs::rename(entry.path(), uuidDir / entry.path().filename(), ec);
                }
                Log::info("File", "✓ Чанки перемещены после таймаута");
            }
        } else {
            Log::info("File", "⏳ Ожидание: прошло ", ageSeconds, "с из ", timeoutSeconds, "с");
        }
    }*/

    return false;
}

bool File::isComplete(uint32_t uuid) const {
    auto it = parts_.find(uuid);
    if (it == parts_.end()) return false;

    const auto& partMap = it->second;
    if (partMap.empty()) return false;

    // Проверяем, есть ли все части от 0 до totalParts-1
    int expectedTotal = partMap.begin()->second.totalParts;
    if (partMap.size() != static_cast<size_t>(expectedTotal)) return false;

    for (int i = 0; i < expectedTotal; ++i) {
        if (partMap.find(i) == partMap.end()) return false;
    }
    return true;
}

std::string File::getCompletePath(uint32_t uuid) const {
    // Этот метод используется после assembleFile, когда файл уже сохранён
    // Возвращает пустую строку, если файл не найден
    return "";
}


std::string File::getPeerIpByUuid(uint32_t uuid) const {
    std::string uuidStr = std::to_string(uuid);
    std::error_code ec;

    // Путь к inbox: <workDir>/<myIp>/inbox/
    fs::path inboxBase = config.workDir;
    inboxBase /= config.myContact.ip.empty() ? "unknown" : config.myContact.ip;
    inboxBase /= "inbox";

    if (!fs::exists(inboxBase, ec)) {
        Log::error("File", "❌ Каталог inbox/ не найден");
        return "unknown";
    }

    // Обходим inbox/<peerIp>/<uuid>/
    for (const auto& ipEntry : fs::directory_iterator(inboxBase, ec)) {
        if (!ipEntry.is_directory()) continue;

        std::string peerIp = ipEntry.path().filename().string();

        // Проверяем, существует ли каталог с этим uuid
        fs::path uuidDir = ipEntry.path() / uuidStr;
        if (fs::exists(uuidDir, ec) && fs::is_directory(uuidDir, ec)) {
            Log::info("File", "✓ Найден IP для uuid=", uuid, ": ", peerIp);
            return peerIp;
        }
    }

    Log::warn("File", "⚠️ Передача uuid=", uuid, " не найдена в inbox/");
    return "unknown";
}

//////////////////////////////////////////////////////////////////////////////////
fs::path File::getOutgoingDir(const std::string& contactIp, uint32_t uuid) const {
    // <workDir>/<myIp>/outbox/<contactIp>/outgoing/<uuid>/
    return getOutboxDir(contactIp) / "outgoing" / std::to_string(uuid);
}
//////////////////////////////////////////////////////////////////////////////////

// ============================================================
// Время модификации файла -> epoch-секунды (для сравнения старшинства)
// ============================================================
uint64_t File::fileTimeToEpoch(const fs::path& p) {

    std::error_code ec;
    auto ftime = fs::last_write_time(p, ec);
    if (ec) return 0;

    // C++17: пересчёт file_time_type в system_clock через «сейчас»
    auto nowFs = fs::file_time_type::clock::now();
    auto nowSys = std::chrono::system_clock::now();
    auto tp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - nowFs + nowSys);

    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count());
}

// ============================================================
// Обход каталогов контактов: по 1 самому старому файлу из каждого.
// Несуществующие каталоги создаются.
// ============================================================
std::vector<File::OutboxFile> File::scanOutboxAll() {
    std::vector<OutboxFile> result;

    for (const auto& contact : config.addressbook) {
        const std::string& ip = contact.ip;
        fs::path dir = getOutboxDir(ip);

        // Каталога нет — создаём и идём дальше (файлов в нём пока нет)
        if (!ensureDir(dir)) continue;

        // Ищем самый старый обычный файл в корне каталога
        std::error_code ec;
        bool found = false;
        OutboxFile oldest;

        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) continue; // spool/ и пр. каталоги не трогаем

            OutboxFile of;
            of.fullPath = entry.path();
            of.filename = entry.path().filename().string();
            of.destIp = ip;
            of.timeCreated = fileTimeToEpoch(entry.path());
            of.size = entry.file_size(ec);

            if (!found || of.timeCreated < oldest.timeCreated) {
                oldest = of;
                found = true;
            }
        }

        if (found) result.push_back(oldest);
    }

    return result;
}

// ============================================================
// Нарезка самого старого файла в spool/<destIp>-<uuid>/,
// исходный файл удаляется. Отправки пока нет.
// ============================================================
uint32_t File::spoolFile(const OutboxFile& of, int chunkSize) {
    // Читаем файл целиком



    std::ifstream in(of.fullPath.string(), std::ios::binary | std::ios::ate);
    if (!in) {
        Log::error("File", "❌ Не удалось открыть файл: ", of.fullPath.string());
        return 0;
    }
    std::streamsize fileSize = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<char> buffer(fileSize);
    if (fileSize > 0 && !in.read(buffer.data(), fileSize)) {
        Log::error("File", "❌ Ошибка чтения файла: ", of.fullPath.string());
        return 0;
    }
    in.close();

    // Проверяем, является ли это файлом пруфа (<uuid>.pwProof)
    uint32_t uuid = 0;
    bool isProofFile = false;
    std::string filename = of.filename;

    // Проверяем расширение .pwProof (регистронезависимо)
    std::string ext = of.fullPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".pwproof") {
        // Это файл пруфа — извлекаем uuid из имени
        isProofFile = true;

        // Имя файла: <uuid>.pwProof
        size_t dotPos = filename.find('.');
        if (dotPos != std::string::npos) {
            std::string uuidStr = filename.substr(0, dotPos);
            try {
                uuid = std::stoul(uuidStr);
                Log::info("File", "📋 Файл пруфа для uuid=", uuid);
            } catch (...) {
                Log::error("File", "❌ Не удалось извлечь uuid из имени: ", filename);
                return 0;
            }
        } else {
            Log::error("File", "❌ Неправильное имя файла пруфа: ", filename);
            return 0;
        }
    } else {
        // Обычный файл — генерируем новый uuid
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist(1, 0xFFFFFF);
        uuid = dist(gen);
    }

    // Каталог spool/<destIp>/<uuid>/
    fs::path spoolDir = getSpoolDir(of.destIp, uuid);
    if (!ensureDir(spoolDir)) return 0;

    // Количество чанков (минимум 1, даже для пустого файла)
    int totalParts = static_cast<int>((fileSize + chunkSize - 1) / chunkSize);
    if (totalParts == 0) totalParts = 1;

    Log::info("File", "📦 Нарезка ", of.filename, " (", fileSize, " байт) на ",
              totalParts, " чанков -> ", spoolDir.string());

    // Сохраняем чанки: <part>-<totalParts>-<filename>
    for (int part = 0; part < totalParts; ++part) {
        std::streamsize start = static_cast<std::streamsize>(part) * chunkSize;
        std::streamsize end = std::min(start + chunkSize, fileSize);
        std::streamsize size = end - start; // может быть 0 для пустого файла




        // Имя чанка: <part>-<totalParts>-<retries>-<filename>, ретраи пока 0
        std::string chunkName = std::to_string(part) + "-" +
                                std::to_string(totalParts) + "-0-" + of.filename;
        fs::path chunkPath = spoolDir / chunkName;


        std::ofstream out(chunkPath.string(), std::ios::binary | std::ios::trunc);
        if (!out) {
            Log::error("File", "❌ Не удалось создать чанк: ", chunkPath.string());
            return 0;
        }
        if (size > 0) out.write(buffer.data() + start, size);
        out.close();
    }

    // Переносим исходный файл из outbox/<destIp>/ в outbox/<destIp>/work/
    fs::path workDir = getOutboxDir(of.destIp) / "work";
    if (!ensureDir(workDir)) {
        Log::error("File", "❌ Не удалось создать каталог work/");
        return 0;
    }

    fs::path workPath = workDir / of.filename;
    std::error_code ec;
    fs::rename(of.fullPath, workPath, ec);
    if (ec) {
        Log::warn("File", "⚠️ Не удалось перенести файл в work/: ", of.fullPath.string());
        return 0;
    }

    Log::info("File", "✓ Файл перенесён в work/: ", workPath.string());
    Log::info("File", "✅ Файл ", of.filename, " в spool (uuid=", uuid, ")");
    return uuid;
}

#include "words.h" // для buildFileFrame

fs::path File::getSpoolBase() const {
    fs::path base = config.workDir;
    base /= config.myContact.ip.empty() ? "unknown" : config.myContact.ip;
    base /= "outbox";
    base /= "spool";
    return base;
}

fs::path File::getSpoolDir(const std::string& destIp, uint32_t uuid) const {
    //return getSpoolBase() / (destIp + "-" + std::to_string(uuid));
    return getSpoolBase() / destIp / std::to_string(uuid);

}

/*
int File::countSpoolTransfers() const {
    fs::path base = getSpoolBase();
    std::error_code ec;
    if (!fs::exists(base, ec)) return 0;
    int n = 0;
    for (const auto& e : fs::directory_iterator(base, ec))
        if (e.is_directory()) n++; // waiting/ лежит глубже, здесь только передачи
    return n;
}*/
int File::countSpoolTransfers() const {
    fs::path base = getSpoolBase();
    std::error_code ec;
    if (!fs::exists(base, ec)) return 0;

    int n = 0;
    for (const auto& ipEntry : fs::directory_iterator(base, ec)) {   // spool/<peerIp>
        if (!ipEntry.is_directory()) continue;
        for (const auto& uuidEntry : fs::directory_iterator(ipEntry.path(), ec)) {
            if (uuidEntry.is_directory()) n++;                       // spool/<peerIp>/<uuid>
        }
    }
    return n;
}

int File::countPwpFiles() const {
    fs::path base = getSpoolBase();
    std::error_code ec;
    if (!fs::exists(base, ec)) return 0;
    int n = 0;
    for (const auto& e : fs::directory_iterator(base, ec))
        if (e.is_regular_file() && e.path().extension() == ".pwp") n++;
    return n;
}

// ОТПРАВКА
void File::prepareWaitingPackets(int maxPwp) {

    // Опакечивание и перенос файлов в /waiting
    // Здесь же проставляется флаг waitingApprove
    // Здесь же он проверяется

    fs::path base = getSpoolBase();
    std::error_code ec;
    if (!fs::exists(base, ec)) return;
/*
    // Обходим все каталоги передач <destIp>-<uuid>
    // Наверно, нужно сделать обход в случайном порядке, чтобы в связи с лимитом
    // файлы не греблись бы из пары первых каталогов

    for (const auto& dirEntry : fs::directory_iterator(base, ec)) {

        if (!dirEntry.is_directory()) continue;


        // Лимит: не больше maxPwp готовых пакетов
        if (countPwpFiles() >= maxPwp) return;


        //Надо сделать ожидание аппрува от дальней стороны
        // Протокол таков:
        //
        // отправка чанка 0>>> | <<<пруф | отправка>>> | всех остальных>>> | чанков>>> | <<<общий пруф на каждый чанк
        // (- досылка утерянных - пруф...)
        //
        //
        //  Если (в <destIp>/<uuid>/waiting лежит 1 файл И в <destIp>-<uuid>/approved пусто, либо каталог не создан) то:
        //      - проверить время создания файла, а так же ретраи в его имени
        //      - если прошло, скажем, 20 минут config.proofTimeout, то:
        //          переписать ретрай в имени у файла в перенести обратно в <destIp>-<uuid>
        //          если ретраи превышены, то просто удалить <destIp>-<uuid> целиком
        //      - иначе выйти
        //
        //

        // Разбор имени каталога: <destIp>/<uuid> (в IP точки, дефис только перед uuid)

        std::string dirName = dirEntry.path().filename().string();
        size_t dash = dirName.rfind('-');
        */

    // Обходим передачи: spool/<peerIp>/<uuid>/
    for (const auto& ipEntry : fs::directory_iterator(base, ec)) {
        if (!ipEntry.is_directory()) continue;

        std::string destIp = ipEntry.path().filename().string();

        for (const auto& uuidEntry : fs::directory_iterator(ipEntry.path(), ec)) {

            if (!uuidEntry.is_directory()) continue;

            uint32_t uuid = 0;
            try {
                uuid = std::stoul(uuidEntry.path().filename().string());
            } catch (...) {
                continue;
            }

            // dirEntry больше нет, работаем с uuidEntry
            fs::path transferDir = uuidEntry.path();

            // Лимит: не больше maxPwp готовых пакетов
            if (countPwpFiles() >= maxPwp) return;

            //////
            ///
            ///
            ///

            //if (dash == std::string::npos) continue;
            //std::string destIp = dirName.substr(0, dash);
            //uint32_t uuid = 0;
            //try { uuid = std::stoul(dirName.substr(dash + 1)); } catch (...) { continue; }

            ///////////////////////////////
            // ШАГ 1: просмотр approved
            ///////////////////////////////
            //fs::path approvedDir = dirEntry.path() / "approved";
            fs::path approvedDir = transferDir / "approved";
            int approvedCount = 0;

            if (fs::exists(approvedDir, ec) && fs::is_directory(approvedDir, ec)) {
                for (const auto& entry : fs::directory_iterator(approvedDir, ec)) {
                    if (entry.is_regular_file()) {
                        approvedCount++;
                    }
                }
            }

            //Log::info("File", "📊 uuid=", uuid, ": approved=", approvedCount, " чанков");

            ///////////////////////////////
            // ШАГ 1.1: просмотр waiting
            ///////////////////////////////
            //fs::path waitingDir = dirEntry.path() / "waiting";
            fs::path waitingDir = transferDir / "waiting";

            if (fs::exists(waitingDir, ec) && fs::is_directory(waitingDir, ec)) {
                // Считаем файлы в waiting/
                std::vector<fs::path> waitingFiles;
                for (const auto& entry : fs::directory_iterator(waitingDir, ec)) {
                    if (entry.is_regular_file()) {
                        waitingFiles.push_back(entry.path());
                    }
                }

                if (!waitingFiles.empty()) {
                    // Есть файлы в waiting/
                    if (approvedCount == 0) {
                        // approved пуст — ждём первый пруф
                        // Берём первый файл (должен быть только один)
                        fs::path waitFile = waitingFiles[0];

                        // Получаем время создания файла
                        uint64_t fileTime = fileTimeToEpoch(waitFile);
                        uint64_t nowTime = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count());

                        uint64_t ageSeconds = nowTime - fileTime;

                        if (ageSeconds >= static_cast<uint64_t>(config.proofTimeout)) {
                            // Таймаут истёк — удаляем всю передачу
                            Log::warn("File", "⏱️ Таймаут ожидания первого пруфа (",
                                      ageSeconds, "с >= ", config.proofTimeout, "с)");
                            Log::warn("File", "❌ Удаляем передачу uuid=", uuid);
                            //fs::remove_all(dirEntry.path(), ec);
                            fs::remove_all(transferDir, ec);
                        } else {
                            // Ждём дальше
                            //Log::info("File", "⏳ Ожидание первого пруфа для uuid=", uuid,
                            //          " (прошло ", ageSeconds, "с из ", config.proofTimeout, "с)");
                        }

                        // Выходим из обработки этого uuid
                        continue;
                    }
                    // Если approved не пуст — продолжаем (переходим к следующему шагу)
                    //Log::info("File", "✓ Первый чанок подтверждён, продолжаем отправку uuid=", uuid);
                }
            }

            // Ищем самый старый чанок в корне каталога (waiting/ — каталог, пропускается)
            // Имя чанка: <part>-<total>-<retries>-<filename>
            struct Cand { fs::path path; int part, total, retries; std::string fname; uint64_t t; };
            std::vector<Cand> cands;

            //for (const auto& ce : fs::directory_iterator(dirEntry.path(), ec)) {
            for (const auto& ce : fs::directory_iterator(transferDir, ec)) {
                if (!ce.is_regular_file()) continue;
                std::string name = ce.path().filename().string();

                size_t d1 = name.find('-');
                size_t d2 = name.find('-', d1 + 1);
                size_t d3 = name.find('-', d2 + 1);
                if (d1 == std::string::npos || d2 == std::string::npos || d3 == std::string::npos) continue;

                try {
                    Cand c;
                    c.part    = std::stoi(name.substr(0, d1));
                    c.total   = std::stoi(name.substr(d1 + 1, d2 - d1 - 1));
                    c.retries = std::stoi(name.substr(d2 + 1, d3 - d2 - 1));
                    c.fname   = name.substr(d3 + 1);
                    c.path    = ce.path();
                    c.t       = fileTimeToEpoch(ce.path());
                    cands.push_back(c);

                    // ОТЛАДКА
                    Log::info("File", "🔍 Найден чанок в корне: ", name, " (fname=", c.fname, "part=", c.part, ", total=", c.total, ")");

                } catch (...) { continue; }
            }

            // ОТЛАДКА
            //Log::info("File", "📊 uuid=", uuid, ": найдено ", cands.size(), " чанков в корне");

            if (cands.empty()) continue; // всё уже в waiting/ или отправлено

            // Самый старый; при равенстве времени — меньший номер части
            auto best = std::min_element(cands.begin(), cands.end(),
                                         [](const Cand& a, const Cand& b) {
                                             if (a.t != b.t) return a.t < b.t;
                                             return a.part < b.part;
                                         });

            // Читаем содержимое чанка
            std::ifstream in(best->path.string(), std::ios::binary);
            if (!in) continue;
            std::vector<uint8_t> content((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
            in.close();

            // Если это pwproof, то fname указать PF

            // Проверяем расширение .pwProof (регистронезависимо)
            std::string ext = best->path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            std::string fileName;

            //у пруфа имя файла задается как PF
            fileName = (ext == ".pwproof")? "PF" : best->fname;


            // Оборачиваем в PW-пакет: магик + frame


            // Находим MAC получателя
            uint16_t destMac = 0;
            for (const auto& contact : config.addressbook) {
                if (contact.ip == destIp) {
                    destMac = contact.mac;
                    break;
                }
            }

            // Здесь нужно будет получить и маршрут (прочитать из <myip>/routes, например)
            // Маршрут нужно записать в вектор uint16_t

            //std::vector<uint8_t> frame = Words::buildFileFrame(

            //Пока сделаем маршрут из одного узла, потом будем делать полноценно
            std::vector<uint16_t> route {config.myContact.mac, destMac};

            std::vector<uint8_t> frame = Words::buildMHFrame(
                route,                        // маршрут отсюда до цели
                config.myContact.mac,           // origin (наш MAC)
                fileName,                       // "report.pdf"
                uuid,


                static_cast<uint8_t>(best->total),
                static_cast<uint8_t>(best->part),
                content);

            std::vector<uint8_t> packet;
            packet.reserve(3 + frame.size());
            packet.push_back('P');
            packet.push_back('W');
            packet.push_back(0x01);
            packet.insert(packet.end(), frame.begin(), frame.end());

            // Переносим чанок в waiting/ (резервная копия для ретраев)
            //fs::path waitingDir = dirEntry.path() / "waiting";
            ensureDir(waitingDir);
            fs::rename(best->path, waitingDir / best->path.filename(), ec);

            // Сохраняем готовый пакет: <part>-<total>-<retries>-<destIp>-<uuid>.pwp
            std::string pwpName = std::to_string(best->part) + "-" +
                                  std::to_string(best->total) + "-" +
                                  std::to_string(best->retries) + "-" +
                                  destIp + "-" + std::to_string(uuid) + ".pwp";
            fs::path pwpPath = base / pwpName;


            Log::info("File", "💾 Создаю .pwp: ", pwpPath.string());


            std::ofstream out(pwpPath.string(), std::ios::binary | std::ios::trunc);
            if (!out) {
                Log::error("File", "❌ Не удалось создать .pwp: ", pwpPath.string());
                continue;
            }
            out.write(reinterpret_cast<const char*>(packet.data()),
                      static_cast<std::streamsize>(packet.size()));
            out.close();

            Log::info("File", "✅ .pwp создан: ", pwpName, " (", packet.size(), " байт)");
        }
    }
}

std::vector<File::PwpFile> File::getPwpFiles() {
    std::vector<PwpFile> result;
    fs::path base = getSpoolBase();
    std::error_code ec;
    if (!fs::exists(base, ec)) return result;

    for (const auto& e : fs::directory_iterator(base, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".pwp") continue;

        std::string name = e.path().filename().string();
        // Формат: <part>-<total>-<retries>-<destIp>-<uuid>.pwp
        size_t d1 = name.find('-');
        size_t d2 = name.find('-', d1 + 1);
        size_t d3 = name.find('-', d2 + 1);
        size_t d4 = name.find('-', d3 + 1);
        if (d1 == std::string::npos || d2 == std::string::npos ||
            d3 == std::string::npos || d4 == std::string::npos) continue;

        try {
            PwpFile pf;
            pf.part    = std::stoi(name.substr(0, d1));
            pf.total   = std::stoi(name.substr(d1 + 1, d2 - d1 - 1));
            pf.retries = std::stoi(name.substr(d2 + 1, d3 - d2 - 1));
            pf.destIp  = name.substr(d3 + 1, d4 - d3 - 1);
            std::string uuidStr = name.substr(d4 + 1);
            uuidStr = uuidStr.substr(0, uuidStr.find(".pwp"));
            pf.uuid = std::stoul(uuidStr);
            pf.path = e.path();
            pf.timeCreated = fileTimeToEpoch(e.path());
            result.push_back(pf);
        } catch (...) { continue; }
    }

    // Сортируем по времени создания
    std::sort(result.begin(), result.end(),
              [](const PwpFile& a, const PwpFile& b) {
                  return a.timeCreated < b.timeCreated;
              });

    return result;
}

void File::removePwp(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        Log::warn("File", "⚠️ Не удалось удалить .pwp: ", path.string());
    } else {
        Log::info("File", "✓ Удалён .pwp: ", path.string());
    }
}

void File::incrementPwpRetry(const fs::path& path) {
    std::string name = path.filename().string();
    size_t d1 = name.find('-');
    size_t d2 = name.find('-', d1 + 1);
    size_t d3 = name.find('-', d2 + 1);
    if (d1 == std::string::npos || d2 == std::string::npos || d3 == std::string::npos) return;

    try {
        int retries = std::stoi(name.substr(d2 + 1, d3 - d2 - 1)) + 1;
        std::string newName = name.substr(0, d2 + 1) +
                              std::to_string(retries) +
                              name.substr(d3);
        fs::path newPath = path.parent_path() / newName;

        std::error_code ec;
        fs::rename(path, newPath, ec);
        if (ec) {
            Log::warn("File", "⚠️ Не удалось переименовать .pwp");
        } else {
            Log::info("File", "🔄 .pwp переименован (retry ", retries, "): ", newPath.string());
        }
    } catch (...) {
        Log::warn("File", "⚠️ Ошибка парсинга имени .pwp");
    }
}
