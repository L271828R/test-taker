#include "session.h"
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
Score ScoreFromString(const std::string& s) {
    if (s == "correct") return Score::Correct;
    if (s == "partial") return Score::Partial;
    if (s == "missed")  return Score::Missed;
    return Score::Skipped;
}

std::string ScoreToString(Score s) {
    switch (s) {
        case Score::Correct: return "correct";
        case Score::Partial: return "partial";
        case Score::Missed:  return "missed";
        default:             return "skipped";
    }
}

std::string ScoreLabel(Score s) {
    switch (s) {
        case Score::Correct: return "Correct";
        case Score::Partial: return "Partial";
        case Score::Missed:  return "Missed";
        default:             return "Skipped";
    }
}

// ---------------------------------------------------------------------------
// Each turn in the block body:
//
//   Q: <question text>
//   A: <answer text, may be empty>
//   SCORE: correct|partial|missed|skipped
//   FLAG: true|false
//   EXPLANATION: <text, single line>
//   <blank line separating turns>
//
std::vector<QuestionTurn> ParseSession(const std::string& body) {
    std::vector<QuestionTurn> turns;
    QuestionTurn cur;
    bool inTurn = false;

    std::istringstream ss(body);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.rfind("Q: ", 0) == 0) {
            if (inTurn) turns.push_back(cur);
            cur     = {};
            inTurn  = true;
            cur.question = line.substr(3);
        } else if (inTurn && line.rfind("A: ", 0) == 0) {
            cur.userAnswer = line.substr(3);
        } else if (inTurn && line.rfind("SCORE: ", 0) == 0) {
            cur.score = ScoreFromString(line.substr(7));
        } else if (inTurn && line.rfind("FLAG: ", 0) == 0) {
            cur.flagged = (line.substr(6) == "true");
        } else if (inTurn && line.rfind("EXPLANATION: ", 0) == 0) {
            cur.explanation = line.substr(13);
        }
    }
    if (inTurn) turns.push_back(cur);
    return turns;
}

// ---------------------------------------------------------------------------
std::string SerializeSessionBody(const std::vector<QuestionTurn>& turns) {
    std::ostringstream out;
    for (const auto& t : turns) {
        out << "Q: "           << t.question    << "\n";
        out << "A: "           << t.userAnswer  << "\n";
        out << "SCORE: "       << ScoreToString(t.score) << "\n";
        out << "FLAG: "        << (t.flagged ? "true" : "false") << "\n";
        out << "EXPLANATION: " << t.explanation << "\n";
        out << "\n";
    }
    return out.str();
}

// ---------------------------------------------------------------------------
static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    return {std::istreambuf_iterator<char>(f), {}};
}

static bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f) return false;
    f << content;
    return f.good();
}

// ---------------------------------------------------------------------------
std::vector<QuestionTurn> LoadSession(const std::string& filePath) {
    std::string content = readFile(filePath);
    if (content.empty()) return {};

    std::istringstream ss(content);
    std::string line;
    bool inSession = false;
    std::string body;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (!inSession && line.rfind(":::session[", 0) == 0) {
            inSession = true;
            body.clear();
        } else if (inSession) {
            if (line == ":::") return ParseSession(body);
            body += line + "\n";
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
bool AppendSessionTurn(const std::string& filePath, const QuestionTurn& turn) {
    std::string content = readFile(filePath);
    if (content.empty()) return false;

    std::string entry = SerializeSessionBody({turn});

    size_t sessPos = content.find(":::session[");
    if (sessPos != std::string::npos) {
        // Append inside existing block before its closing :::
        size_t closePos = content.find("\n:::", sessPos);
        if (closePos == std::string::npos) return false;
        content.insert(closePos + 1, entry);
    } else {
        // Create new block at end of file
        if (!content.empty() && content.back() != '\n') content += '\n';
        content += "\n:::session[Session]\n" + entry + ":::\n";
    }

    return writeFile(filePath, content);
}

// ---------------------------------------------------------------------------
bool SetTurnFlagged(const std::string& filePath, int index, bool flagged) {
    std::string content = readFile(filePath);
    if (content.empty()) return false;

    size_t sessPos = content.find(":::session[");
    if (sessPos == std::string::npos) return false;

    size_t headerEnd = content.find('\n', sessPos);
    if (headerEnd == std::string::npos) return false;
    size_t bodyStart = headerEnd + 1;

    size_t closeNl = content.find("\n:::", sessPos);
    if (closeNl == std::string::npos) return false;

    std::string body = content.substr(bodyStart, closeNl + 1 - bodyStart);
    auto turns = ParseSession(body);
    if (index < 0 || index >= (int)turns.size()) return false;

    turns[index].flagged = flagged;
    std::string newBody = SerializeSessionBody(turns);

    // Rebuild: header + newBody + closing :::
    std::string header = content.substr(sessPos, bodyStart - sessPos);
    std::string tail   = content.substr(closeNl + 1); // starts at :::
    content = content.substr(0, sessPos) + header + newBody + tail;

    return writeFile(filePath, content);
}
