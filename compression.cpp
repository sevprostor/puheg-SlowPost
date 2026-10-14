#include "compression.h"
#include "log.h"
#include <zstd.h>
#include <fstream>
#include <vector>

bool Compression::compressFile(const fs::path& pathIn,
                               const fs::path& pathOut,
                               int level) {
    // Читаем исходный файл в память
    std::ifstream in(pathIn.string(), std::ios::binary | std::ios::ate);
    if (!in) {
        Log::error("Compression", "❌ Не удалось открыть: ", pathIn.string());
        return false;
    }

    std::streamsize srcSize = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> src(srcSize);
    if (srcSize > 0 && !in.read(reinterpret_cast<char*>(src.data()), srcSize)) {
        Log::error("Compression", "❌ Ошибка чтения: ", pathIn.string());
        return false;
    }
    in.close();

    // Выделяем буфер под сжатые данные (с запасом)
    size_t dstCapacity = ZSTD_compressBound(srcSize);
    std::vector<uint8_t> dst(dstCapacity);

    // Сжимаем
    size_t dstSize = ZSTD_compress(dst.data(), dstCapacity,
                                   src.data(), srcSize,
                                   level);

    if (ZSTD_isError(dstSize)) {
        Log::error("Compression", "❌ Ошибка сжатия: ",
                   ZSTD_getErrorName(dstSize));
        return false;
    }

    // Записываем результат
    std::ofstream out(pathOut.string(), std::ios::binary | std::ios::trunc);
    if (!out) {
        Log::error("Compression", "❌ Не удалось создать: ", pathOut.string());
        return false;
    }
    out.write(reinterpret_cast<const char*>(dst.data()),
              static_cast<std::streamsize>(dstSize));
    out.close();

    Log::info("Compression", "✓ Сжато: ", srcSize, " → ", dstSize,
              " байт (", pathIn.filename().string(), ")");

    return true;
}

bool Compression::decompressFile(const fs::path& pathIn,
                                 const fs::path& pathOut) {
    // Читаем сжатый файл
    std::ifstream in(pathIn.string(), std::ios::binary | std::ios::ate);
    if (!in) {
        Log::error("Compression", "❌ Не удалось открыть: ", pathIn.string());
        return false;
    }

    std::streamsize srcSize = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> src(srcSize);
    if (srcSize > 0 && !in.read(reinterpret_cast<char*>(src.data()), srcSize)) {
        Log::error("Compression", "❌ Ошибка чтения: ", pathIn.string());
        return false;
    }
    in.close();

    // Получаем размер распакованных данных из заголовка zstd
    unsigned long long dstCapacity = ZSTD_getFrameContentSize(src.data(), srcSize);
    if (dstCapacity == ZSTD_CONTENTSIZE_ERROR ||
        dstCapacity == ZSTD_CONTENTSIZE_UNKNOWN) {
        // Если размер неизвестен — пробуем с большим запасом
        dstCapacity = srcSize * 10;
        Log::warn("Compression", "⚠️ Размер содержимого неизвестен, выделяю ",
                  dstCapacity, " байт");
    }

    std::vector<uint8_t> dst(dstCapacity);

    // Распаковываем
    size_t dstSize = ZSTD_decompress(dst.data(), dstCapacity,
                                     src.data(), srcSize);

    if (ZSTD_isError(dstSize)) {
        Log::error("Compression", "❌ Ошибка распаковки: ",
                   ZSTD_getErrorName(dstSize));
        return false;
    }

    // Записываем результат
    std::ofstream out(pathOut.string(), std::ios::binary | std::ios::trunc);
    if (!out) {
        Log::error("Compression", "❌ Не удалось создать: ", pathOut.string());
        return false;
    }
    out.write(reinterpret_cast<const char*>(dst.data()),
              static_cast<std::streamsize>(dstSize));
    out.close();

    Log::info("Compression", "✓ Распаковано: ", srcSize, " → ", dstSize,
              " байт (", pathIn.filename().string(), ")");

    return true;
}

bool Compression::isZstdFile(const fs::path& path) {
    // Магические байты zstd: 0xFD 0x2F 0xB5 0x28 (little-endian)
    std::ifstream in(path.string(), std::ios::binary);
    if (!in) return false;

    uint8_t magic[4] = {0};
    if (!in.read(reinterpret_cast<char*>(magic), 4)) return false;
    in.close();

    return magic[0] == 0x28 && magic[1] == 0xB5 &&
           magic[2] == 0x2F && magic[3] == 0xFD;
}
