#include "address.h"
#include "log.h"

#include <sstream>
#include <vector>

// Разбивает строку по разделителю
static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, delim)) {
        parts.push_back(token);
    }
    return parts;
}

// Проверка, что строка — валидный октет (0..255)
static bool isValidOctet(const std::string& s, int& value) {
    if (s.empty() || s.size() > 3) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    try {
        value = std::stoi(s);
    } catch (...) {
        return false;
    }
    return value >= 0 && value <= 255;
}

uint16_t Address::ipToMac(const std::string& ipStr) {
    std::vector<std::string> parts = split(ipStr, '.');
    if (parts.size() != 4) {
        Log::warn("Address", "Неверный формат IP: ", ipStr);
        return 0;
    }

    int octets[4];
    for (int i = 0; i < 4; ++i) {
        if (!isValidOctet(parts[i], octets[i])) {
            Log::warn("Address", "Неверный октет в IP: ", ipStr);
            return 0;
        }
    }

    // Первый октет должен быть 10
    if (octets[0] != 10) {
        Log::warn("Address", "IP не из подсети 10.x.x.x: ", ipStr);
        return 0;
    }

    // Второй октет зарезервирован, должен быть 0
    if (octets[1] != 0) {
        Log::warn("Address", "Второй октет зарезервирован (должен быть 0): ", ipStr);
        return 0;
    }

    // MAC = последние два октета
    uint16_t mac = static_cast<uint16_t>((octets[2] << 8) | octets[3]);
    return mac;
}

std::string Address::macToIp(uint16_t mac) {
    int high = (mac >> 8) & 0xFF;
    int low  = mac & 0xFF;

    std::string ip = "10.0." + std::to_string(high) + "." + std::to_string(low);
    return ip;
}
