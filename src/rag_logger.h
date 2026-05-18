#pragma once
#include <string>
#include <fstream>
#include <vector>

struct RagChunk {
    float       score = 0.0f;
    std::string doc;
    std::string text;
};

// Appends structured RAG retrieval events to ~/Library/Logs/TestTaker/rag.log.
class RagLogger {
public:
    static RagLogger& get();
    void logEvent(const std::string& context,
                  const std::string& query,
                  const std::vector<RagChunk>& chunks);
private:
    RagLogger();
    std::ofstream m_file;
};
