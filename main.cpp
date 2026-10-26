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
#include "stats.h"
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

    Log::info("SlowPost", "***************************************************");
    Log::info("SlowPost", "Puheg-SlowPost 0.0.1 build ", __DATE__, " ", __TIME__);
    Log::info("SlowPost", "  + вынос uuid на конверт");
    Log::info("SlowPost", "  + сохранение статистики входящих передач");
    Log::info("SlowPost", "В работе:");
    Log::info("SlowPost", "  * 1. сохранение исходящей статистики");
    Log::info("SlowPost", "  2. сохранение маршрутов, инкремент попыток");
    Log::info("SlowPost", "  3. сервисы");
    Log::info("SlowPost", "***************************************************");
    Log::info("ListenWords", "Слушаю EventBus на порту ", config.eventBusPort, "...");


    File file;
    RouteManager router;
    bool inited = false;


    while (true) {

        // Задержка между итерациями главного цикла
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        EBMessage emsg = transport.poll(100);
        //uint64_t config.nowEpoch() = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        //        std::chrono::system_clock::now().time_since_epoch()).count());

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

        Config::unhangBusy(config.nowEpoch());
        RunningProcess::unhangProcess(config.nowEpoch());

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

            //auto now = std::chrono::steady_clock::now();
            //auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastProcessComplete).count();
            //auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - runningProc.lastProcessCompleted).count();
            uint64_t elapsed = config.nowEpoch() - runningProc.lastProcessCompleted;

            // Задержка: sendTXDelay + random(0..sendTXDelay)
            static std::random_device rd;
            static std::mt19937 gen(rd());
            int rndizer = gen() % 1000;

            ////////////////////////////////////////////////////
            ///
            /// Начало рабочего такта отправки
            ///

            // от половины до полутора делеев

            if (elapsed >= config.sendTXDelay / 2 + ((rndizer * config.sendTXDelay) / 1000)) {


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


                    // Ищем первый .pwp, который можно отправить
                    const File::PwpFile* toSend = nullptr;

                    for (const auto& pwp : pwpFiles) {
                        if (pwp.retries == 0) {
                            // Первая отправка — можно сразу
                            toSend = &pwp;
                            break;
                        } else {
                            // Ретрай — проверяем задержку

                            uint64_t minSendTime = pwp.timeCreated + config.sendRetryDelay;
                            static bool printed = false;

                            if (config.nowEpoch() >= minSendTime ){
                                toSend = &pwp;
                                break;
                            } else {
                                // Ещё рано, пропускаем этот файл

                                uint64_t waitSeconds = minSendTime - config.nowEpoch();

                                if(!printed){

                                    printed = true;
                                    Log::info("Main", pwp.path.filename().string(),
                                          ": ретрай, отправка через ", waitSeconds, "с");
                                }
                            }
                        }
                    }


                    if(toSend && !config.driverState.busy){
                        const auto& oldest = *toSend;

                        // Читаем содержимое .pwp
                        std::ifstream in(oldest.path.string(), std::ios::binary);
                        if (in) {
                            std::vector<uint8_t> packet((std::istreambuf_iterator<char>(in)),
                                                        std::istreambuf_iterator<char>());
                            in.close();

                            Words words;

                            // распарсить конверт, чтоб запомнить в процессе тип
                            Envelope sentEnv;
                            std::vector<uint8_t> body(packet.begin() + 3, packet.end());

                            size_t esize = words.parseEnv(body, sentEnv);
                            //uint16_t chunkSize = body.size() - 3 - esize;
                            uint16_t peer = sentEnv.fdest;

                            // Отправляем
                            // Не отправлять, если идет прием или передача
                            if (transport.sendFrame(oldest.destIp, packet)) {
                                Log::info("Main", oldest.path.filename().string(), " отправлен в адаптер: ",
                                          body.size() - 3 - esize, " байт, part ",
                                          oldest.part + 1, "/", oldest.total,
                                          ", uuid ", oldest.uuid,
                                          ", peer ", std::to_string(peer),
                                          oldest.retries > 0 ? ", retry " + std::to_string(oldest.retries) : "",
                                          ")");

                                // Начать новый процесс
                                runningProc.processingUUID = oldest.uuid;
                                runningProc.processingFile = oldest.path.filename();
                                runningProc.peerMAC = peer;
                                runningProc.fileSize = body.size() - 3 - esize;
                                runningProc.processingType = sentEnv.type;
                                runningProc.running = true;
                                runningProc.state = "";
                                runningProc.lastActivity = config.nowEpoch();
                                runningProc.startTime = config.nowEpoch();
                                //runningProc.lastActivity = std::chrono::steady_clock::now(); //добавлено время запуска

                            } else {
                                Log::error("ListenWords", "❌ Ошибка отправки .pwp");
                            }
                        }
                    }
                }
            }


        } else {

            ////////////////////////////////////////////////////////////////////////
            /// Добавить сброс, если процесс не стартовал в течение 1 секунды.
            /// Переотправка произойдет сама штатными средствами

            //auto now = std::chrono::steady_clock::now();
            //auto wasActive = std::chrono::duration_cast<std::chrono::milliseconds>(now - runningProc.lastActivity).count();

            uint64_t wasActive = config.nowEpoch() - runningProc.lastActivity;

            //Сброс, если зупущен новый процесс, но дреайвер или адаптер не отозвались
            //if (runningProc.running && !runningProc.accepted && wasActive > 5000){

            if (runningProc.running && !runningProc.accepted && wasActive > 5){
                //runningProc.running = false;
                runningProc.state = "FAIL";
                Log::error("Main", "Процесс не стартовал, сброс");

            }

            ////////////////////////////////////////////////////////////////////////

            // Процесс активен — проверяем завершение
            if (runningProc.state == "OK" || runningProc.state == "FAIL" || runningProc.state == "WORK") {


                //runningProc.lastProcessCompleted = std::chrono::steady_clock::now();
                runningProc.lastProcessCompleted = config.nowEpoch();

                // Находим соответствующий .pwp
                auto pwpFiles = file.getPwpFiles();
                for (const auto& pwp : pwpFiles) {

                    Log::info("Main", "uuid ", pwp.uuid,
                              ", state ", runningProc.state,
                              ", procUUID ", std::to_string(runningProc.processingUUID),
                              ", thread ", runningProc.thread,
                              ", file ", runningProc.processingFile.string(),
                              ", type ", runningProc.processingType);


                    if (pwp.uuid == runningProc.processingUUID) {

                        //Если процесс только что запущен, присвоить статус принятого
                        if (!runningProc.accepted){

                            runningProc.accepted = true;
                            runningProc.lastActivity = config.nowEpoch();
                        }

                        //Если процесс завершился неудачей, то переименовать ретрай и больше ничего не делать
                        if (runningProc.state == "FAIL"){
                            runningProc.running = false;

                            Log::fail("Main", pwp.path.filename().string(), " не отправлен");
                            file.incrementPwpRetry(pwp.path);


                        }



                        if (runningProc.state == "OK") {

                            runningProc.running = false;

                            Log::success("Main", pwp.path.filename().string(), " отправлен");

                            // === ПРОВЕРКА: это файл пруфа? ===
                            // Рекурсивно ищем <uuid>.pwproof в outbox/
                            // (с учётом маршрутизации pwp.destIp ≠ origin)

                            fs::path outboxDir = fs::path(config.workDir) /
                                                 (config.myContact.ip.empty() ? "unknown" : config.myContact.ip) /
                                                 "outbox";

                            std::error_code ec;


                            if (runningProc.processingType == 'P' || runningProc.processingType == 'p') {
                                Log::debug("Main", "Успешно отправлен пруф типа '",
                                           runningProc.processingType, "'", std::to_string(runningProc.processingUUID));

                                // При отправке пруфа на него мы не ждем никаких подтверждений.
                                // Поэтому, как только пакет успешно отправился, мы проверяем inbox -
                                // закончился ли прием этой передачи, на которую послан пруф

                                Proof::cleanInbox(pwp.uuid);

                            }

                            file.removePwp(pwp.path);
                        }

                        Statistics::updateOutgoingStats();

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
