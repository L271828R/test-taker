#pragma once
#include <string>
#include <fstream>

// Appends timestamped lines to ~/Library/Logs/TestTaker/test-taker.log.
class Logger {
public:
    static Logger& get();
    void log(const std::string& msg);
private:
    Logger();
    std::ofstream m_file;
};
