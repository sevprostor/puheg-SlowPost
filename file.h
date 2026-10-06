#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "words.h"
#include <filesystem>

namespace fs = std::filesystem;



class File {
public:
    File();

    struct OutboxFile {
        fs::path fullPath;
        std::string filename;
        std::string destIp;
        uint64_t timeCreated;
        size_t size;
    };

    // Сборка файла из чанков (существующий метод)
    bool assembleFile(const PwFile& pwFile);

    void proofAvailableChunks();

    // НОВОЕ: проверить, завершён ли файл с данным uuid
    bool isComplete(uint32_t uuid) const;

    // НОВОЕ: получить путь к завершённому файлу
    std::string getCompletePath(uint32_t uuid) const;

    // НОВОЕ: сканировать outbox/<contactIp>/ и вернуть список файлов для отправки

    //std::vector<OutboxFile> scanOutbox(const std::string& contactIp);


    struct Chunk {
        fs::path path;
        int part;
        int totalParts;
        std::string filename;
        uint32_t uuid;
    };


    // НОВОЕ: по 1 самому старому файлу из каждого каталога контакта;
    // несуществующие каталоги создаются
    std::vector<OutboxFile> scanOutboxAll();

    // НОВОЕ: нарезать файл на чанки в outbox/spool/<destIp>-<uuid>/,
    // исходный файл удалить. Возвращает uuid (0 = ошибка)
    uint32_t spoolFile(const OutboxFile& of, int chunkSize = 2000);

    // НОВОЕ: сколько каталогов передач в spool (ограничитель шага 3)
    int countSpoolTransfers() const;

    // НОВОЕ: сколько *.pwp лежит в корне spool (ограничитель шага 4)
    int countPwpFiles() const;

    // НОВОЕ: шаг 4 — из каждого <destIp>-<uuid> взять самый старый чанк,
    // перенести в waiting/, обернуть в PW-пакет и сохранить как
    // spool/<part>-<total>-<retries>-<destIp>-<uuid>.pwp
    void prepareWaitingPackets(int maxPwp = 4);



    // НОВОЕ: получить список .pwp файлов
    struct PwpFile {
        fs::path path;
        int part;
        int total;
        int retries;
        std::string destIp;
        uint32_t uuid;
        uint64_t timeCreated;
    };
    std::vector<PwpFile> getPwpFiles();

    // НОВОЕ: удалить .pwp файл
    void removePwp(const fs::path& path);

    // НОВОЕ: переименовать .pwp, увеличив счётчик ретраев
    void incrementPwpRetry(const fs::path& path);

    bool incrementApprove(uint32_t uuid);

    //ex private:
    // <workDir>/<myIp>/outbox/spool/
    fs::path getSpoolBase() const;
    // Создание каталога, если не существует
    bool ensureDir(const fs::path& dir) const;
    // Путь к директории исходящих файлов: <workDir>/<myIp>/outbox/<contactIp>/
    fs::path getOutboxDir(const std::string& contactIp) const;

    std::string getPeerIpByUuid(uint32_t uuid) const;

private:

    static uint64_t fileTimeToEpoch(const fs::path& p);



    // <workDir>/<myIp>/outbox/spool/<destIp>-<uuid>/
    fs::path getSpoolDir(const std::string& destIp, uint32_t uuid) const;


    // Путь к каталогу чанков: <workDir>/<myIp>/outbox/<contactIp>/outgoing/<uuid>/
    fs::path getOutgoingDir(const std::string& contactIp, uint32_t uuid) const;

    // Путь к директории входящих файлов: <workDir>/<myIp>/inbox/<senderIp>/
    //fs::path getInboxDir(uint16_t senderId) const;
    fs::path getInboxDir(const std::string& senderIp) const;




    // Путь к директории чанков: <workDir>/<myIp>/outbox/<contactIp>/outgoing/<filename>/
    fs::path getOutgoingDir(const std::string& contactIp, const std::string& filename) const;



    // Хранилище частей файлов: uuid -> map<part, PwFile>
    std::map<uint32_t, std::map<int, PwFile>> parts_;
};
