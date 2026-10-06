#include "config.h"
#include <fstream>
#include <iostream>
#include <string>
#include <cctype>

//Config config;

bool Config::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(0, 1);

        if (key == "eventBusPort") {
            eventBusPort = static_cast<uint16_t>(std::stoi(value));
        }
    }
    return true;
}

bool Config::parseCommandLine(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            return false;
        }
        else if (arg == "-config" && i + 1 < argc) {
            configPath = argv[++i];
        }
        else if (arg == "-eb" && i + 1 < argc) {
            eventBusPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
    }
    return true;
}
