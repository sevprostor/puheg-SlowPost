#include "proof.h"
#include "words.h"
#include "file.h"
#include "config.h"
#include "log.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>

namespace fs = std::filesystem;

bool Proof::getTransferDir(uint32_t uuid, fs::path& transferDir){

    File file;
    //fs::path transferDir;

    fs::path spoolBase = file.getSpoolBase();
    std::error_code ec;

    if (!fs::exists(spoolBase, ec)) {
        Log::error("Proof", "❌ Каталог spool/ не найден");
        return false;
    }

    std::string uuidStr = std::to_string(uuid);

    bool found = false;

    // Обходим все каталоги: spool/<peerIp>/<uuid>/
    for (const auto& ipEntry : fs::directory_iterator(spoolBase, ec)) {
        if (!ipEntry.is_directory()) continue;

        for (const auto& uuidEntry : fs::directory_iterator(ipEntry.path(), ec)) {
            if (!uuidEntry.is_directory()) continue;

            if (uuidEntry.path().filename().string() == uuidStr) {
                transferDir = uuidEntry.path();
                found = true;
                //Log::info("Proof", "✓ Найдена передача uuid=", uuid,
                //          " в ", transferDir.string());
                break;
            }
        }

        if (found) break;
    }

    if (!found) {
        Log::error("Proof", "❌ Передача uuid=", uuid, " не найдена в spool/");
        return false;
    }

    return true;

}

void Proof::approveChunks(uint32_t uuid, std::vector<uint8_t> approvedChunks){

    std::error_code ec;

    fs::path transferDir;

    if(!getTransferDir(uuid, transferDir)) return;

    fs::path waitingDir = transferDir / "waiting";
    fs::path approvedDir = transferDir / "approved";

    // Создаём set для быстрого поиска, игнорируя заполнители 0xFF
    std::set<int> approvedSet;
    for (uint8_t b : approvedChunks) {
        if (b == 0xFF) continue;
        approvedSet.insert(static_cast<int>(b));
    }

    int movedCount = 0;

    // Обходим чанки в waiting/ и переносим подтверждённые в approved/
    for (const auto& entry : fs::directory_iterator(waitingDir, ec)) {
        if (!entry.is_regular_file()) continue;

        std::string name = entry.path().filename().string();

        // Парсим номер части: <part>-<total>-<retries>-<filename>
        size_t d1 = name.find('-');
        if (d1 == std::string::npos) continue;

        int part = 0;
        try {
            part = std::stoi(name.substr(0, d1));
        } catch (...) {
            continue;
        }

        // Проверяем, есть ли эта часть в списке подтверждённых
        if (approvedSet.find(part) != approvedSet.end()) {
            fs::rename(entry.path(), approvedDir / name, ec);
            if (ec) {
                Log::warn("Proof", "⚠️ Не удалось перенести чанок ", part, " в approved/");
            } else {
                movedCount++;
                Log::info("Proof", "✓ Чанок ", part, " перенесён в approved/");

                // Удаляем соответствующий .pwp файл
                fs::path pwpPath = transferDir / (name + ".pwp");
                if (fs::exists(pwpPath, ec)) {
                    fs::remove(pwpPath, ec);
                    if (!ec) {
                        Log::info("Proof", "✓ Удалён .pwp для чанка ", part);
                    }
                }
            }
        }else{
            // То, чего нету в пруфе - выкидываем обратно в transferDir
            fs::rename(entry.path(), transferDir / name, ec);
            if (ec) {
                Log::warn("Proof", "⚠️ Не удалось перенести чанок ", part, " в parent/");
            } else {
                movedCount++;
                Log::info("Proof", "✓ Чанок ", part, " перенесён в parent/");

                // Удаляем соответствующий .pwp файл
                fs::path pwpPath = transferDir / (name + ".pwp");
                if (fs::exists(pwpPath, ec)) {
                    fs::remove(pwpPath, ec);
                    if (!ec) {
                        Log::info("Proof", "✓ Удалён .pwp для чанка ", part);
                    }
                }
            }
        }
    }


    std::string aparts;
    for(int i = 0; i < approvedChunks.size(); i++){
        aparts += std::to_string(approvedChunks[i]);
        aparts += ",";
    }
    Log::info("Proof", "📊 Обработаны чанки ", aparts, " из пруфа uuid=", uuid);


}

bool Proof::isTransmissionCompleted(uint32_t uuid){

    std::error_code ec;

    fs::path transferDir;

    if(!getTransferDir(uuid, transferDir)) return false;

    fs::path approvedDir = transferDir / "approved";

    // Проверяем, завершена ли передача
    int totalInApproved = 0;
    int totalExpected = 0;

    for (const auto& entry : fs::directory_iterator(approvedDir, ec)) {
        if (!entry.is_regular_file()) continue;

        totalInApproved++;

        std::string name = entry.path().filename().string();
        size_t d1 = name.find('-');
        size_t d2 = name.find('-', d1 + 1);

        if (d1 != std::string::npos && d2 != std::string::npos) {
            try {
                totalExpected = std::stoi(name.substr(d1 + 1, d2 - d1 - 1));
                //break;
            } catch (...) {}
        }
    }

    Log::info("Proof", "expected: ", totalExpected, ", approved: ", totalInApproved);


    if (totalExpected > 0 && totalInApproved == totalExpected) {
        Log::info("Proof", "✅ Передача uuid=", uuid, " полностью подтверждена (",
                  totalInApproved, "/", totalExpected, " чанков)");

        return true;
    }

    return false;

}

void Proof::getOriginalFName(uint32_t uuid, std::string& original){

    // Имя оригинального файла — из имени любого чанка в approved/

    std::error_code ec;

    fs::path transferDir;

    if(!getTransferDir(uuid, transferDir)) return;

    fs::path approvedDir = transferDir / "approved";

    for (const auto& entry : fs::directory_iterator(approvedDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();

        //здесь нужно отрезать part-oftotal-
        //то есть по второй - включительно.

        // Ищем первый дефис (после part)
        size_t firstDash = name.find('-');
        if (firstDash == std::string::npos) continue;

        // Ищем второй дефис (после total)
        size_t secondDash = name.find('-', firstDash + 1);
        if (secondDash == std::string::npos) continue;

        // 3й дефис
        size_t thridDash = name.find('-', secondDash + 1);
        if (thridDash == std::string::npos) continue;
        // Всё после второго дефиса — это имя файла
        original = name.substr(thridDash + 1);

        /*
        size_t lastDash = name.rfind('-');
        if (lastDash != std::string::npos) {
            original = name.substr(lastDash + 1);
            break;
        }
        */


    }

    return;
}

bool Proof::processIncomingProof(uint32_t uuid, const std::vector<uint8_t>& approvedParts) {

    File file;
    std::error_code ec;

    fs::path transferDir;

    if(!getTransferDir(uuid, transferDir)) return false;

    std::string uuidStr = std::to_string(uuid);
    fs::path peerIpPath = transferDir.parent_path();
    std::string peerIp = peerIpPath.filename().string();

    fs::path waitingDir = transferDir / "waiting";
    fs::path approvedDir = transferDir / "approved";

    if (!file.ensureDir(approvedDir)) {
        Log::error("Proof", "❌ Не удалось создать каталог approved/");
        return false;
    }

    approveChunks(uuid, approvedParts);

    // Проверяем, завершена ли передача

    if(isTransmissionCompleted(uuid)){

        std::string originalFilename;
        getOriginalFName(uuid, originalFilename);

        if (originalFilename.empty()) {
            Log::warn("Proof", "⚠️ Не удалось определить имя оригинального файла");
        } else {
            //Log::info("Proof", "📄 Оригинальный файл: ", originalFilename);

            // Проверяем, не proof-ли это файл (proof-файлы не переносим в sent)
            std::string ext = fs::path(originalFilename).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            bool isProofFile = (ext == ".pwproof");

            // === ШАГ 3: перенос в outbox/<peerIp>/sent/ ===
            if (!isProofFile) {

                moveToSent(uuid);

            } else {

                //Log::info("Proof", "📋 Это proof-файл, в sent не переносим");
                cleanInbox(uuid);
            }

        }

        cleanSpool(uuid);

    }

    return true;
}

void Proof::cleanInbox(uint32_t uuid){

    // Удаляем входящую передачу в inbox/<peerIp>/<uuid>/
    // если там не осталось ничего, кроме tmp/route

    // Извлекаем uuid из имени proof-файла
    // Возможные форматы:
    //   0-1-0-<uuid>.pwproof  (формат pwp)
    //   <uuid>.pwproof        (простой формат)

    //std::string uuidStr;

    File file;
    fs::path transferDir;

    if(!getTransferDir(uuid, transferDir)) return;

    fs::path waitingDir = transferDir / "waiting";
    fs::path approvedDir = transferDir / "approved";
    std::string uuidStr = std::to_string(uuid);
    fs::path peerIpPath = transferDir.parent_path();
    std::string peerIp = peerIpPath.filename().string();

    std::error_code ec;


    // Имя оригинального файла — из имени любого чанка в approved/
    std::string originalFilename;
    getOriginalFName(uuid, originalFilename);

    fs::path origPath = originalFilename;
    std::string baseName = origPath.stem().string();  // без расширения

    fs::path uuidDir = fs::path(config.workDir) /
                       (config.myContact.ip.empty() ? "unknown" : config.myContact.ip) /
                       "inbox" / peerIp / uuidStr;

    if (fs::exists(uuidDir, ec)) {
        // Проверяем, что в каталоге нет ничего, кроме tmp/
        bool onlyTmp = true;
        fs::path tmpDir = uuidDir / "tmp";

        for (const auto& entry : fs::directory_iterator(uuidDir, ec)) {
            if (entry.path() == tmpDir) continue;
            onlyTmp = false;
            break;
        }

        if (onlyTmp) {
            fs::remove_all(uuidDir, ec);
            if (ec) {
                Log::warn("Proof", "⚠️ Не удалось удалить каталог uuid: ",
                          ec.message());
            } else {
                Log::info("Proof", "✓ Удалён каталог входящей передачи: ",
                          uuidDir.string());
            }
        } else {
            Log::info("Proof", "⏳ В каталоге uuid остались другие файлы, "
                               "не удаляем: ", uuidDir.string());
        }
    } else {
        Log::warn("Proof", "⚠️ Каталог входящей передачи не найден: ",
                  uuidDir.string());
    }
    //}

    // Удаляем proof-файл из work/
    fs::path workPath = file.getOutboxDir(peerIp) / "work" / originalFilename;
    if (fs::exists(workPath, ec)) {
        fs::remove(workPath, ec);
        if (!ec) {
            Log::info("Proof", "✓ Proof-файл удалён из work/: ", workPath.string());
        } else {
            Log::warn("Proof", "⚠️ Не удалось удалить proof-файл: ", ec.message());
        }
    } else {
        Log::warn("Proof", "⚠️ Proof-файл не найден в work/: ", workPath.string());
    }

}

void Proof::cleanSpool(uint32_t uuid){

    // === ШАГ 0: Удаляем оставшиеся .pwp файлы (ретраи) для этого uuid из корня spool/<peerIp>/ ===
    std::string uuidStr = std::to_string(uuid);

    std::string pwpSuffix = "-" + uuidStr + ".pwp";
    std::error_code ec;

    fs::path transferDir;

    if(!getTransferDir(uuid, transferDir)) return;

    //std::string uuidStr = std::to_string(uuid);
    fs::path peerIpPath = transferDir.parent_path();
    std::string peerIp = peerIpPath.filename().string();

    if (fs::exists(peerIpPath, ec) && fs::is_directory(peerIpPath, ec)) {
        for (const auto& entry : fs::directory_iterator(peerIpPath, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".pwp") {
                std::string filename = entry.path().filename().string();

                // Проверяем, что имя файла заканчивается на "-<uuid>.pwp"
                // Это надёжнее, чем парсить все дефисы, и защищает от коллизий
                if (filename.length() >= pwpSuffix.length() &&
                    filename.compare(filename.length() - pwpSuffix.length(), pwpSuffix.length(), pwpSuffix) == 0) {

                    fs::remove(entry.path(), ec);
                    if (!ec) {
                        Log::info("Proof", "✓ Удалён оставшийся ретрай .pwp: ", filename);
                    } else {
                        Log::warn("Proof", "⚠️ Не удалось удалить .pwp ", filename, ": ", ec.message());
                    }
                }
            }
        }
    }

    // === ШАГ 1: удаляем каталог uuid в spool ===
    fs::remove_all(transferDir, ec);
    if (ec) {
        Log::warn("Proof", "⚠️ Не удалось удалить каталог uuid в spool: ", ec.message());
    } else {
        Log::info("Proof", "✓ Каталог uuid удалён из spool: ", transferDir.string());
    }

    // === ШАГ 2: если каталог <peerIp> в spool пуст — удалить его ===
    if (fs::exists(peerIpPath, ec) && fs::is_directory(peerIpPath, ec)) {
        bool isEmpty = true;
        for (const auto& e : fs::directory_iterator(peerIpPath, ec)) {
            (void)e;  // подавляем warning unused
            isEmpty = false;
            break;
        }
        if (isEmpty) {
            fs::remove(peerIpPath, ec);
            if (!ec) {
                Log::info("Proof", "✓ Пустой каталог IP удалён из spool: ", peerIpPath.string());
            } else {
                Log::warn("Proof", "⚠️ Не удалось удалить пустой каталог IP: ", ec.message());
            }
        }
    }
}

void Proof::moveToSent(uint32_t uuid){


    File file;
    fs::path transferDir;

    if(!getTransferDir(uuid, transferDir)) return;

    fs::path peerIpPath = transferDir.parent_path();
    std::string peerIp = peerIpPath.filename().string();
    std::string originalFilename;

    getOriginalFName(uuid, originalFilename);

    std::error_code ec;



    fs::path sentDir = file.getOutboxDir(peerIp) / "sent";
    if (file.ensureDir(sentDir)) {

        // Ищем файл с суффиксом .zstd в work/
        fs::path workPath = file.getOutboxDir(peerIp) / "work" / (originalFilename + ".zstd");
        //fs::path workPath = file.getOutboxDir(peerIp) / "work" / originalFilename + ".zstd";
        fs::path sentPath = sentDir / (originalFilename + ".zstd");

        if (fs::exists(workPath, ec)) {
            fs::rename(workPath, sentPath, ec);
            if (ec) {
                Log::warn("Proof", "⚠️ Не удалось перенести в sent/: ", ec.message());
            } else {
                Log::info("Proof", "✓ Файл перенесён в sent/: ", sentPath.string());
            }
        } else {
            Log::warn("Proof", "⚠️ Файл не найден в work/: ", workPath.string());
        }
    }

}

//bool Proof::sendFileProof(uint16_t senderMac, uint32_t uuid, std::vector<int>& parts) {
bool Proof::sendFileProof(std::string senderIp, uint32_t uuid, std::vector<int>& parts) {

    if(senderIp == "unknown") return false;

    Log::info("Proof", "📤 Отправка пруфа для uuid=", uuid);

    // Находим IP отправителя

    //Надо посмотреть во входящих - найти каталог, содержащий uuid

    // Путь к каталогу outbox/<senderIp>/
    fs::path outboxDir = fs::path(config.workDir) /
                         (config.myContact.ip.empty() ? "unknown" : config.myContact.ip) /
                         "outbox" / senderIp;

    // Создаём каталог если не существует
    std::error_code ec;
    if (!fs::exists(outboxDir, ec)) {
        fs::create_directories(outboxDir, ec);
        if (ec) {
            Log::error("Proof", "❌ Не удалось создать каталог outbox/", senderIp);
            return false;
        }
    }

    // Путь к файлу пруфа: outbox/<senderIp>/<uuid>.pwProof
    fs::path proofPath = outboxDir / (std::to_string(uuid) + ".pwproof");

    // Содержимое: один байт с номером части
    std::vector<uint8_t> content;
    std::string partsForLog;

    for(int i = 0; i < parts.size(); i++){
        content.push_back(static_cast<uint8_t>(parts[i]));
        partsForLog += std::to_string(static_cast<uint8_t>(parts[i])) + ",";
    }

    // Записываем файл
    std::ofstream out(proofPath.string(), std::ios::binary | std::ios::trunc);
    if (!out) {
        Log::error("Proof", "❌ Не удалось создать файл пруфа: ", proofPath.string());
        return false;
    }
    out.write(reinterpret_cast<const char*>(content.data()), content.size());
    out.close();

    Log::info("Proof", "✓ Файл пруфа создан: ", proofPath.string(),
              " (uuid=", uuid, ", parts=", partsForLog, ")");
    return true;
}

