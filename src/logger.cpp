#include "logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <unistd.h>

Logger& Logger::get() {
    static Logger instance;
    return instance;
}

Logger::Logger() : m_pid(getpid()) {
    std::string dir = std::string(getenv("HOME") ?: "") + "/Library/Logs/TestTaker";
    ::system(("mkdir -p \"" + dir + "\"").c_str());
    m_file.open(dir + "/test-taker.log", std::ios::app);
    // Mark each run so sessions are distinguishable in the shared appended file.
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::ostringstream hdr;
    hdr << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S")
        << " [" << m_pid << "]  === SESSION START ===\n";
    m_file << hdr.str();
    m_file.flush();
}

void Logger::log(const std::string& msg) {
    if (!m_file.is_open()) return;
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::ostringstream line;
    line << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S")
         << " [" << m_pid << "]  " << msg << "\n";
    m_file << line.str();
    m_file.flush();
}
