#ifndef COMPRESSION_H
#define COMPRESSION_H
#pragma once
#include <filesystem>
#include <cstdint>

namespace fs = std::filesystem;

class Compression {
public:
    // Сжать файл pathIn → pathOut с уровнем level.
    // Возвращает true при успехе.
    static bool compressFile(const fs::path& pathIn,
                             const fs::path& pathOut,
                             int level);

    // Распаковать файл pathIn → pathOut.
    // Возвращает true при успехе.
    static bool decompressFile(const fs::path& pathIn,
                               const fs::path& pathOut);

    // Проверить, является ли файл zstd-сжатым (по магическим байтам).
    static bool isZstdFile(const fs::path& path);


};
#endif // COMPRESSION_H
