#include "session.h"
#include <fstream>
#include <functional>
#include <sstream>

// Encode embedded newlines as the two-character sequence \n so every field
// occupies exactly one line in the serialized format.
static std::string encodeNewlines(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n"; }
        else { out += c; }
    }
    return out;
}

static std::string decodeNewlines(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i+1] == 'n')  { out += '\n'; ++i; }
            else if (s[i+1] == '\\') { out += '\\'; ++i; }
            else { out += s[i]; }
        } else {
            out += s[i];
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
Score ScoreFromString(const std::string& s) {
    if (s == "1") return Score::Star1;
    if (s == "2") return Score::Star2;
    if (s == "3") return Score::Star3;
    if (s == "4") return Score::Star4;
    if (s == "5") return Score::Star5;
    // Backward compat with old session files
    if (s == "correct") return Score::Star5;
    if (s == "partial") return Score::Star3;
    if (s == "missed")  return Score::Star1;
    return Score::Skipped;
}

std::string ScoreToString(Score s) {
    switch (s) {
        case Score::Star1:   return "1";
        case Score::Star2:   return "2";
        case Score::Star3:   return "3";
        case Score::Star4:   return "4";
        case Score::Star5:   return "5";
        default:             return "skipped";
    }
}

std::string ScoreLabel(Score s) {
    switch (s) {
        case Score::Star1:   return "\xe2\x98\x85\xe2\x98\x86\xe2\x98\x86\xe2\x98\x86\xe2\x98\x86"; // ★☆☆☆☆
        case Score::Star2:   return "\xe2\x98\x85\xe2\x98\x85\xe2\x98\x86\xe2\x98\x86\xe2\x98\x86"; // ★★☆☆☆
        case Score::Star3:   return "\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85\xe2\x98\x86\xe2\x98\x86"; // ★★★☆☆
        case Score::Star4:   return "\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85\xe2\x98\x86"; // ★★★★☆
        case Score::Star5:   return "\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85"; // ★★★★★
        default:             return "\xe2\x80\x94"; // —
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
            cur.question = decodeNewlines(line.substr(3));
        } else if (inTurn && line.rfind("A: ", 0) == 0) {
            cur.userAnswer = decodeNewlines(line.substr(3));
        } else if (inTurn && line.rfind("SCORE: ", 0) == 0) {
            cur.score = ScoreFromString(line.substr(7));
        } else if (inTurn && line.rfind("FLAG: ", 0) == 0) {
            cur.flagged = (line.substr(6) == "true");
        } else if (inTurn && line.rfind("EXPLANATION: ", 0) == 0) {
            cur.explanation = decodeNewlines(line.substr(13));
        } else if (inTurn && line.rfind("NOTE: ", 0) == 0) {
            cur.note = decodeNewlines(line.substr(6));
        } else if (inTurn && line.rfind("SAVED: ", 0) == 0) {
            cur.saved = (line.substr(7) == "true");
        }
    }
    if (inTurn) turns.push_back(cur);
    return turns;
}

// ---------------------------------------------------------------------------
std::string SerializeSessionBody(const std::vector<QuestionTurn>& turns) {
    std::ostringstream out;
    for (const auto& t : turns) {
        out << "Q: "           << encodeNewlines(t.question)    << "\n";
        out << "A: "           << encodeNewlines(t.userAnswer)  << "\n";
        out << "SCORE: "       << ScoreToString(t.score) << "\n";
        out << "FLAG: "  << (t.flagged ? "true" : "false") << "\n";
        out << "SAVED: " << (t.saved   ? "true" : "false") << "\n";
        out << "EXPLANATION: " << encodeNewlines(t.explanation) << "\n";
        if (!t.note.empty())
            out << "NOTE: " << encodeNewlines(t.note) << "\n";
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
SessionHeader LoadSessionHeader(const std::string& filePath) {
    SessionHeader hdr;
    std::string content = readFile(filePath);
    if (content.empty()) return hdr;

    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("**Topic:** ", 0) == 0)
            hdr.topic = line.substr(11);
        else if (line.rfind("**Instructions:** ", 0) == 0)
            hdr.instructions = line.substr(18);
        else if (line.rfind("**Difficulty:** ", 0) == 0)
            hdr.difficulty = line.substr(16);
        else if (line.rfind("**Questions:** ", 0) == 0) {
            try { hdr.totalQuestions = std::stoi(line.substr(15)); } catch (...) {}
        } else if (line.rfind("**Backend:** ", 0) == 0)
            hdr.backend = line.substr(13);
        else if (line.rfind(":::session[", 0) == 0)
            break; // stop at session block
    }
    return hdr;
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
static bool RewriteTurns(const std::string& filePath,
                         std::function<void(std::vector<QuestionTurn>&)> mutate) {
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

    mutate(turns);

    std::string newBody = SerializeSessionBody(turns);
    std::string header  = content.substr(sessPos, bodyStart - sessPos);
    std::string tail    = content.substr(closeNl + 1);
    content = content.substr(0, sessPos) + header + newBody + tail;
    return writeFile(filePath, content);
}

// ---------------------------------------------------------------------------
bool SetTurnFlagged(const std::string& filePath, int index, bool flagged) {
    return RewriteTurns(filePath, [index, flagged](std::vector<QuestionTurn>& turns) {
        if (index >= 0 && index < (int)turns.size())
            turns[index].flagged = flagged;
    });
}

// ---------------------------------------------------------------------------
bool SetTurnNote(const std::string& filePath, int index, const std::string& note) {
    return RewriteTurns(filePath, [index, &note](std::vector<QuestionTurn>& turns) {
        if (index >= 0 && index < (int)turns.size())
            turns[index].note = note;
    });
}

// ---------------------------------------------------------------------------
bool SetTurnSaved(const std::string& filePath, int index, bool saved) {
    return RewriteTurns(filePath, [index, saved](std::vector<QuestionTurn>& turns) {
        if (index >= 0 && index < (int)turns.size())
            turns[index].saved = saved;
    });
}
