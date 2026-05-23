#include "turn_chat.h"
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
std::vector<TurnChatTurn> ParseTurnChat(const std::string& body) {
    std::vector<TurnChatTurn> turns;
    TurnChatTurn cur;
    bool inAnswer = false;

    std::istringstream ss(body);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.rfind("Q: ", 0) == 0) {
            if (!cur.question.empty()) turns.push_back(cur);
            cur = {line.substr(3), ""};
            inAnswer = false;
        } else if (line.rfind("A: ", 0) == 0) {
            cur.answer = line.substr(3);
            inAnswer = true;
        } else if (inAnswer && !line.empty()) {
            cur.answer += "\n" + line;
        }
    }
    if (!cur.question.empty()) turns.push_back(cur);
    return turns;
}

// ---------------------------------------------------------------------------
std::string SerializeTurnChatBody(const std::vector<TurnChatTurn>& turns) {
    std::ostringstream out;
    for (const auto& t : turns) {
        out << "Q: " << t.question << "\n";
        out << "A: " << t.answer  << "\n\n";
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
std::vector<TurnChatTurn> LoadTurnChat(const std::string& filePath, int turnIndex) {
    std::string content = readFile(filePath);
    if (content.empty()) return {};

    std::string marker = ":::chat[" + std::to_string(turnIndex) + "]";

    std::istringstream ss(content);
    std::string line;
    bool inBlock = false;
    std::string body;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (!inBlock && line == marker) {
            inBlock = true;
            body.clear();
        } else if (inBlock) {
            if (line == ":::") return ParseTurnChat(body);
            body += line + "\n";
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
bool AppendTurnChatTurn(const std::string& filePath, int turnIndex,
                        const TurnChatTurn& turn) {
    std::string content = readFile(filePath);
    // Don't bail on empty — file may not exist yet (e.g. saved_discuss.md)

    std::string marker  = ":::chat[" + std::to_string(turnIndex) + "]";
    std::string newEntry = "Q: " + turn.question + "\nA: " + turn.answer + "\n\n";

    size_t blockPos = content.find(marker);
    if (blockPos != std::string::npos) {
        size_t closePos = content.find("\n:::", blockPos);
        if (closePos == std::string::npos) return false;
        content.insert(closePos + 1, newEntry);
    } else {
        // Append a new block at end of file
        if (!content.empty() && content.back() != '\n') content += '\n';
        content += "\n" + marker + "\n" + newEntry + ":::\n";
    }

    return writeFile(filePath, content);
}

// ---------------------------------------------------------------------------
std::string BuildTurnChatPrompt(const QuestionTurn& examTurn,
                                const std::vector<TurnChatTurn>& history,
                                const std::string& question,
                                const std::string& corpusContext) {
    std::ostringstream out;

    if (!corpusContext.empty())
        out << "## Study corpus context\n\n" << corpusContext << "\n";

    out << "## Exam context\n\n"
        << "The student just completed this exam question:\n\n"
        << "**Question:** " << examTurn.question << "\n"
        << "**Student's answer:** "
        << (examTurn.userAnswer.empty() ? "(skipped — did not know)" : examTurn.userAnswer) << "\n"
        << "**Score:** " << ScoreLabel(examTurn.score) << "\n"
        << "**Examiner's explanation:** " << examTurn.explanation << "\n\n"
        << "## Your role\n\n"
        << "You are a knowledgeable tutor helping the student understand this result. "
           "Answer their follow-up questions clearly and concisely. "
           "Draw on the exam context above and your broader knowledge. "
           "Keep answers to a short paragraph unless depth is needed.\n\n";

    if (!history.empty()) {
        out << "## Conversation so far\n\n";
        for (const auto& t : history) {
            out << "Q: " << t.question << "\nA: " << t.answer << "\n\n";
        }
    }

    out << "## New question\n\nQ: " << question << "\nA:";
    return out.str();
}
