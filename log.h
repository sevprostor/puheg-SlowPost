#ifndef LOG_H
#define LOG_H
#pragma once
#include <iostream>
#include <string>

#include <sstream>
#include <chrono>
#include <iomanip>


// ANSI-коды для цветного вывода в терминале
#define COLOR_RESET   "\033[0m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"

class Log {
public:

    // Вспомогательный метод для получения текущей метки времени
    static std::string getTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tmBuf;
        localtime_r(&time, &tmBuf);

        std::ostringstream oss;
        oss << std::put_time(&tmBuf, "%H:%M:%S");
        return oss.str();
    }

    // Вспомогательный метод для вывода аргументов
    template<typename... Args>
    static void printArgs(Args&&... args) {
        (std::cout << ... << std::forward<Args>(args));
    }

    // ═══════════════════════════════════════════════════════
    // INFO — белый цвет
    // ═══════════════════════════════════════════════════════
    template<typename... Args>
    static void info(const std::string& tag, Args&&... args) {
        std::cout << getTimestamp() << " [" << tag << "] ";
        printArgs(std::forward<Args>(args)...);
        std::cout << std::endl;
    }

    // ═══════════════════════════════════════════════════════
    // DEBUG — белый цвет
    // ═══════════════════════════════════════════════════════
    template<typename... Args>
    static void debug(const std::string& tag, Args&&... args) {
        std::cout << getTimestamp() << " [" << tag << "] ";
        printArgs(std::forward<Args>(args)...);
        std::cout << std::endl;
    }

    // ═══════════════════════════════════════════════════════
    // WARN — жёлтый цвет
    // ═══════════════════════════════════════════════════════
    template<typename... Args>
    static void warn(const std::string& tag, Args&&... args) {
        std::cout << getTimestamp() << COLOR_YELLOW << " [" << tag << "] WARN ";
        printArgs(std::forward<Args>(args)...);
        std::cout  << COLOR_RESET << std::endl;
    }

    // ═══════════════════════════════════════════════════════
    // ERROR — красный цвет
    // ═══════════════════════════════════════════════════════
    template<typename... Args>
    static void error(const std::string& tag, Args&&... args) {
        std::cout << getTimestamp()<< COLOR_RED << " [" << tag << "] ERRR ";
        printArgs(std::forward<Args>(args)...);
        std::cout << COLOR_RESET << std::endl;
    }

    // ═══════════════════════════════════════════════════════
    // FAIL — красный цвет
    // ═══════════════════════════════════════════════════════
    template<typename... Args>
    static void fail(const std::string& tag, Args&&... args) {
        std::cout  << getTimestamp() << COLOR_RED << " [" << tag << "] FAIL ";
        printArgs(std::forward<Args>(args)...);
        std::cout << COLOR_RESET << std::endl;
    }

    // ═══════════════════════════════════════════════════════
    // SUCCESS — зелёный цвет
    // ═══════════════════════════════════════════════════════
    template<typename... Args>
    static void success(const std::string& tag, Args&&... args) {
        std::cout << getTimestamp() << COLOR_GREEN << " [" << tag << "] SUCC ";
        printArgs(std::forward<Args>(args)...);
        std::cout << COLOR_RESET << std::endl;
    }

    // ═══════════════════════════════════════════════════════
    // RX — зелёный цвет (приём данных)
    // ═══════════════════════════════════════════════════════
    template<typename... Args>
    static void rx(const std::string& tag, Args&&... args) {
        std::cout << getTimestamp() << " [" << tag << "] " << COLOR_GREEN << "RX " << COLOR_RESET;
        printArgs(std::forward<Args>(args)...);
        std::cout << std::endl;
    }

    // ═══════════════════════════════════════════════════════
    // TX — красный цвет (передача данных)
    // ═══════════════════════════════════════════════════════
    template<typename... Args>
    static void tx(const std::string& tag, Args&&... args) {
        std::cout << getTimestamp() << " [" << tag << "] " << COLOR_RED << "TX " << COLOR_RESET;
        printArgs(std::forward<Args>(args)...);
        std::cout << std::endl;
    }


};

#endif // LOG_H



