#ifndef PILOT_LOG_H
#define PILOT_LOG_H

#include <iostream>
#include <string>

class Log {
public:
    template<typename... Args>
    static void i(const std::string &msg, Args&&...) {
        std::cout << msg << std::endl;
    }

    template<typename... Args>
    static void e(const std::string &msg, Args&&...) {
        std::cerr << msg << std::endl;
    }
};

#endif
