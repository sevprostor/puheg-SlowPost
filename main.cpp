#include "config.h"
#include "transport.h"
#include "log.h"      // <-- единый вывод
#include "libs/json.hpp"
#include "words.h"
#include "file.h"
#include "ebparser.h"
#include "ebparser.h"
#include "proof.h"
#include "routemanager.h"

#include "process.h"
#include <string>
#include <thread>
#include <fstream>
#include <random>

//File file;
Config config;

// Глобальное состояние для отслеживания процессов
// переделать на runningProc

RunningProcess runningProc;

//uint64_t g_currentThread = 0;
//std::string g_processState = "";
//std::chrono::steady_clock::time_point g_lastProcessComplete;
//bool g_processActive = false;

//Words words;
//File file;


using json = nlohmann::json;

static void usage(const char* prog) {
    Log::info("ListenWords", "listenwords — приём событий шины и PW-кадров");
    Log::info("ListenWords", "  ", prog, " [-eb port] [-config path]");
}

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

int main(int argc, char** argv) {


    // Предварительный проход: ищем -config
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-config" && i + 1 < argc)
            config.configPath = argv[i+1];
    }

    // Загружаем конфиг и применяем аргументы командной строки
    config.loadFromFile(config.configPath);
    if (!config.parseCommandLine(argc, argv)) {
        usage(argv[0]);
        return 1;
    }

    Transport transport;
    if (!transport.init(config.eventBusPort)) {
        Log::error("ListenWords", "Не удалось инициализировать Transport на порту ",
                   config.eventBusPort);
        return 1;
    }

    Log::info("ListenWords", "Слушаю EventBus на порту ", config.eventBusPort, "...");


    File file;
    RouteManager router;
    bool inited = false;


    while (true) {

        // Задержка между итерациями главного цикла
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        EBMessage emsg = transport.poll(100);

        //Слушать эвентбас, разбирать сообщение
        if (emsg.evenbus) EBParser::parseEmsg(emsg, file); //поправить название!

        //пока адресная книга пуста - больше ничего не делать
        if (config.addressbook.empty()){

            continue;

        }else if(!inited){

            inited = true;

            //Проверить наличие
            router.initRoutesFile();

            // TODO
            // Просмотреть адресную книгу, и если там есть чтото новое, то добавить в маршруты.
            // Каталоги в outbox построить исходя из наличия MACов на концах маршрутов.
            // Позже при любых изменениях маршрутов, при необходимости создавать новые outbox/ip

        }


        // Если адресная книга загружена — сканируем outbox и отправляем файлы

        file.proofAvailableChunks(); //не пора ли отправить пруфы?
        //Выкинуть транзитный чанок в спул
        file.moveForwardToSpool();

        // 1. По 1 самому старому файлу из каждого каталога контакта
        auto candidates = file.scanOutboxAll();

        // 2+3. Новый spool — только если в spool < 4 каталогов передач
        if (!candidates.empty() && file.countSpoolTransfers() < config.maxTransfers) {

            auto oldest = std::min_element(candidates.begin(), candidates.end(),
                                           [](const File::OutboxFile& a, const File::OutboxFile& b) {
                                               return a.timeCreated < b.timeCreated;
                                           });
            //file.spoolFile(*oldest, 2000);
            file.spoolFile(*oldest, config.chunkSize);

        }



        // 4. Готовим .pwp — только если в spool нет активных пакетов.
        // ВАЖНО: шаг 4 живёт вне проверки candidates: передачи могут
        // доготавливаться, даже когда outbox уже пуст
        file.prepareWaitingPackets(config.maxTransfers);

        // Процессы теперь приходят адресно - чужой процесс сюда не придет
        // после отправки взять первый тред для отслеживания процесса

        // Если никакой процесс не идет, и со времени завершения предыдущего прошло
        // более (config.sendTXDelay=1sec + random(config.sendTXDelay/2)), то идет отправка.
        // 1. Просмотреть очередь. Пвп с ретраями > config.maxProcessRetries=2 - удалить
        // 1. Взять из очереди самый старый пвп и отправить, при этом получить номер треда
        // 2. Когда процесс с нашим тредом закончится ОК или ФЕЙЛ, запомнить время.
        //  - при неудаче переименовать файл, добавив ретрай. Чтобы он стал самым новым в папке
        //  - при успехе удалить пвп



        // 5. ОТПРАВКА: если процесс не активен и прошло достаточно времени
        //if (!g_processActive) {
        if(!runningProc.running && !config.driverState.busy){

            auto now = std::chrono::steady_clock::now();
            //auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastProcessComplete).count();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - runningProc.lastProcessCompleted).count();

            // Задержка: sendTXDelay + random(0..sendTXDelay)
            static std::random_device rd;
            static std::mt19937 gen(rd());
            //int rndizer = config.sendTXDelay + (gen() % config.sendTXDelay);
            int rndizer = gen() % 1000;

            ////////////////////////////////////////////////////
            ///
            /// Начало рабочего такта отправки
            ///

            if (elapsed >= config.sendTXDelay + (rndizer * 5)) {


                // Удаляем .pwp с retries > maxProcessRetries
                // Просматривается количество ретраев в имени файла "part-total-retries-file.name"

                auto pwpFiles = file.getPwpFiles();

                // Удаляем файлы с превышенным лимитом ретраев
                for (const auto& pwp : pwpFiles) {
                    if (pwp.retries > config.maxProcessRetries) {
                        Log::warn("ListenWords", "❌ .pwp превысил лимит ретраев, удаляем: ",
                                  pwp.path.string());
                        file.removePwp(pwp.path);
                    }
                }

                // Обновляем список после удаления
                pwpFiles = file.getPwpFiles();

                if (!pwpFiles.empty()) {
                    // Получаем текущее время в формате epoch (секунды)
                    uint64_t nowEpoch = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());

                    // Ищем первый .pwp, который можно отправить
                    const File::PwpFile* toSend = nullptr;

                    for (const auto& pwp : pwpFiles) {
                        if (pwp.retries == 0) {
                            // Первая отправка — можно сразу
                            toSend = &pwp;
                            break;
                        } else {
                            // Ретрай — проверяем задержку

                            int r = rndizer / 50;
                            uint64_t minSendTime = pwp.timeCreated + config.sendRetryDelay + r;
                            static bool printed = false;

                            if (nowEpoch >= minSendTime ){
                                toSend = &pwp;
                                break;
                            } else {
                                // Ещё рано, пропускаем этот файл

                                uint64_t waitSeconds = minSendTime - nowEpoch;

                                if(!printed){

                                    printed = true;
                                    Log::info("ListenWords", "⏳ Ретрай ", pwp.path.filename().string(),
                                          ": ждём ещё примерно", waitSeconds, "с");
                                }
                            }
                        }
                    }

                    if (!toSend) {
                        // Нет файлов, готовых к отправке (все ждут задержки)
                        // Продолжаем цикл, попробуем в следующей итерации
                    } else {
                        const auto& oldest = *toSend;

                        // Читаем содержимое .pwp
                        std::ifstream in(oldest.path.string(), std::ios::binary);
                        if (in) {
                            std::vector<uint8_t> packet((std::istreambuf_iterator<char>(in)),
                                                        std::istreambuf_iterator<char>());
                            in.close();

                            // Отправляем
                            if (transport.sendFrame(oldest.destIp, packet)) {
                                Log::info("ListenWords", "📤 Отправлен .pwp: ", oldest.path.filename().string(),
                                          " (part ", oldest.part + 1, "/", oldest.total,
                                          ", uuid=", oldest.uuid,
                                          oldest.retries > 0 ? ", retry=" + std::to_string(oldest.retries) : "",
                                          ")");

                                // Начать новый процесс
                                runningProc.processingUUID = oldest.uuid;
                                runningProc.running = true;
                                runningProc.state = "";
                            } else {
                                Log::error("ListenWords", "❌ Ошибка отправки .pwp");
                            }
                        }
                    }
                }
            }


        } else {
            // Процесс активен — проверяем завершение
            //if (g_processState == "OK" || g_processState == "FAIL") {
            if (runningProc.state == "OK" || runningProc.state == "FAIL") {
                //g_lastProcessComplete = std::chrono::steady_clock::now();
                runningProc.lastProcessCompleted = std::chrono::steady_clock::now();
                //g_processActive = false;
                runningProc.running = false;

                // Находим соответствующий .pwp
                auto pwpFiles = file.getPwpFiles();
                for (const auto& pwp : pwpFiles) {

                    Log::info("ListenWords", "смотрим pwp - ", pwp.uuid, " proc - ", runningProc.thread);


                    if (pwp.uuid == runningProc.processingUUID) {



                        //Если процесс завершился неудачей, то переименовать ретрай и больше ничего не делать
                        if (runningProc.state == "FAIL"){
                            file.incrementPwpRetry(pwp.path);
                        }



                        if (runningProc.state == "OK") {


                            Log::info("SlowPost", "✅ .pwp успешно отправлен: ",
                                      pwp.path.filename().string());

                            // === ПРОВЕРКА: это файл пруфа? ===
                            // Рекурсивно ищем <uuid>.pwproof в outbox/
                            // (с учётом маршрутизации pwp.destIp ≠ origin)
                            fs::path outboxDir = fs::path(config.workDir) /
                                                 (config.myContact.ip.empty() ? "unknown" : config.myContact.ip) /
                                                 "outbox";

                            std::error_code ec;
                            bool isProofFile = false;
                            fs::path proofPath;

                            std::string targetName = std::to_string(pwp.uuid) + ".pwproof";

                            // Рекурсивный поиск файла <uuid>.pwproof в outbox/
                            std::function<bool(const fs::path&)> searchProof =
                                [&](const fs::path& dir) -> bool {
                                if (!fs::exists(dir, ec)) return false;

                                for (const auto& entry : fs::directory_iterator(dir, ec)) {
                                    if (entry.is_directory()) {
                                        if (searchProof(entry.path())) return true;
                                    } else if (entry.is_regular_file() &&
                                               entry.path().filename() == targetName) {
                                        proofPath = entry.path();
                                        return true;
                                    }
                                }
                                return false;
                            };

                            isProofFile = searchProof(outboxDir);

                            if (isProofFile) {
                                Log::info("SlowPost", "📋 Это файл пруфа: ",
                                          proofPath.string());

                                std::vector<uint8_t> parts;
                                parts.emplace_back(pwp.part);
                                Proof::processIncomingProof(pwp.uuid, parts);
                            } //else {
                                //Log::info("SlowPost", "⏳ Ожидание пруфа от получателя");
                            //}

                            file.removePwp(pwp.path);
                        }

                        break;
                    }
                }

                runningProc.thread = 0;
                runningProc.state = "";
            }
        }



    }




        return 0;
    }
