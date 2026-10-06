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


bool Proof::processIncomingProof(uint32_t uuid, const std::vector<uint8_t>& approvedParts) {

    File file;

    fs::path spoolBase = file.getSpoolBase();
    std::error_code ec;

    //just for debug
    std::string aparts;
    for(int i = 0; i < approvedParts.size(); i++){
        aparts += std::to_string(approvedParts[i]);
        aparts += ",";
    }

    if (!fs::exists(spoolBase, ec)) {
        Log::error("Proof", "❌ Каталог spool/ не найден");
        return false;
    }

    std::string uuidStr = std::to_string(uuid);
    fs::path transferDir;
    bool found = false;

    // Обходим все каталоги: spool/<peerIp>/<uuid>/
    for (const auto& ipEntry : fs::directory_iterator(spoolBase, ec)) {
        if (!ipEntry.is_directory()) continue;

        for (const auto& uuidEntry : fs::directory_iterator(ipEntry.path(), ec)) {
            if (!uuidEntry.is_directory()) continue;

            if (uuidEntry.path().filename().string() == uuidStr) {
                transferDir = uuidEntry.path();
                found = true;
                Log::info("Proof", "✓ Найдена передача uuid=", uuid,
                          " в ", transferDir.string());
                break;
            }
        }

        if (found) break;
    }

    if (!found) {
        Log::error("Proof", "❌ Передача uuid=", uuid, " не найдена в spool/");
        return false;
    }

    fs::path waitingDir = transferDir / "waiting";
    fs::path approvedDir = transferDir / "approved";

    if (!file.ensureDir(approvedDir)) {
        Log::error("Proof", "❌ Не удалось создать каталог approved/");
        return false;
    }

    // Создаём set для быстрого поиска, игнорируя заполнители 0xFF
    std::set<int> approvedSet;
    for (uint8_t b : approvedParts) {
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

    Log::info("Proof", "📊 Обработаны чанки ", aparts, " из пруфа uuid=", uuid);

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

        // === ШАГ 1: извлекаем информацию из transferDir ===
        // transferDir = spool/<peerIp>/<uuid>
        fs::path peerIpPath = transferDir.parent_path();
        std::string peerIp = peerIpPath.filename().string();

        // Имя оригинального файла — из имени любого чанка в approved/
        std::string originalFilename;
        for (const auto& entry : fs::directory_iterator(approvedDir, ec)) {
            if (!entry.is_regular_file()) continue;
            std::string name = entry.path().filename().string();
            size_t lastDash = name.rfind('-');
            if (lastDash != std::string::npos) {
                originalFilename = name.substr(lastDash + 1);
                break;
            }
        }

        if (originalFilename.empty()) {
            Log::warn("Proof", "⚠️ Не удалось определить имя оригинального файла");
        } else {
            Log::info("Proof", "📄 Оригинальный файл: ", originalFilename);

            // Проверяем, не proof-ли это файл (proof-файлы не переносим в sent)
            std::string ext = fs::path(originalFilename).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            bool isProofFile = (ext == ".pwproof");

            // === ШАГ 3: перенос в outbox/<peerIp>/sent/ ===
            if (!isProofFile) {
                fs::path sentDir = file.getOutboxDir(peerIp) / "sent";
                if (file.ensureDir(sentDir)) {
                    fs::path workPath = file.getOutboxDir(peerIp) / "work" / originalFilename;
                    fs::path sentPath = sentDir / originalFilename;

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
            } else {
                Log::info("Proof", "📋 Это proof-файл, в sent не переносим");

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

        }

        // === ШАГ 1 (продолжение): удаляем каталог uuid в spool ===
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
                }
            }
        }
    }

    return true;
}

//bool Proof::sendFileProof(uint16_t senderMac, uint32_t uuid, std::vector<int>& parts) {
bool Proof::sendFileProof(std::string senderIp, uint32_t uuid, std::vector<int>& parts) {

    if(senderIp == "unknown") return false;

    Log::info("Proof", "📤 Отправка пруфа для uuid=", uuid);

    // Находим IP отправителя
    /*
    std::string senderIp = "unknown";
    for (const auto& contact : config.addressbook) {
        if (contact.mac == mac) {
            senderIp = contact.ip;
            break;
        }
    }

    if (senderIp == "unknown") {
        Log::error("Proof", "❌ Отправитель не найден в адресной книге");
        return false;
    }
    */

    //File file;

    //std::string senderIp = file.getPeerIpByUuid(uuid);

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

