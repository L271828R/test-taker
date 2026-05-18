#include "rag_logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdlib>

RagLogger& RagLogger::get() {
    static RagLogger instance;
    return instance;
}

RagLogger::RagLogger() {
    std::string dir = std::string(getenv("HOME") ?: "") + "/Library/Logs/TestTaker";
    ::system(("mkdir -p \"" + dir + "\"").c_str());
    m_file.open(dir + "/rag.log", std::ios::app);
}

void RagLogger::logEvent(const std::string& context,
                          const std::string& query,
                          const std::vector<RagChunk>& chunks) {
    if (!m_file.is_open()) return;

    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ts;
    ts << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");

    m_file << "RAG_EVENT\n"
           << "time=" << ts.str() << "\n"
           << "context=" << context << "\n"
           << "query=" << query << "\n";

    for (const auto& c : chunks) {
        std::ostringstream score;
        score << std::fixed << std::setprecision(3) << c.score;
        m_file << "CHUNK score=" << score.str() << " doc=" << c.doc << "\n"
               << c.text << "\n";
    }

    m_file << "END_EVENT\n\n";
    m_file.flush();
}
