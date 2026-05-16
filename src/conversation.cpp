#include "conversation.h"
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
std::vector<ConversationTurn> ParseConversation(const std::string& body) {
    std::vector<ConversationTurn> turns;
    ConversationTurn cur;
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
std::string SerializeConversationBody(const std::vector<ConversationTurn>& turns) {
    std::ostringstream out;
    for (const auto& t : turns) {
        out << "Q: " << t.question << "\n";
        out << "A: " << t.answer  << "\n\n";
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// Scan the file to find the :::conversation block that belongs to chId.
// We track which <!-- ch:N --> precedes each :::conversation block.
std::vector<ConversationTurn> LoadConversation(const std::string& filePath, int chId) {
    std::ifstream f(filePath);
    if (!f) return {};
    std::string content((std::istreambuf_iterator<char>(f)), {});

    std::istringstream ss(content);
    std::string line;
    int curChId = -1;
    bool inConv = false;
    std::string convBody;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.rfind("<!-- ch:", 0) == 0) {
            size_t end = line.find(" -->", 8);
            if (end != std::string::npos) {
                try { curChId = std::stoi(line.substr(8, end - 8)); } catch (...) {}
            }
            if (inConv) { inConv = false; convBody.clear(); } // reset on new chapter
        }

        if (!inConv && curChId == chId && line.rfind(":::conversation[", 0) == 0) {
            inConv = true;
            convBody.clear();
        } else if (inConv) {
            if (line == ":::") return ParseConversation(convBody);
            convBody += line + "\n";
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
bool AppendTurn(const std::string& filePath, int chId,
                const std::string& chTitle, const ConversationTurn& turn) {
    std::ifstream fin(filePath);
    if (!fin) return false;
    std::string content((std::istreambuf_iterator<char>(fin)), {});
    fin.close();

    std::string chMarker = "<!-- ch:" + std::to_string(chId) + " -->";
    size_t chPos = content.find(chMarker);
    if (chPos == std::string::npos) return false;

    size_t nextChPos = content.find("\n<!-- ch:", chPos + chMarker.size());
    size_t chapterEnd = (nextChPos == std::string::npos) ? content.size() : nextChPos;

    std::string newEntry = "Q: " + turn.question + "\nA: " + turn.answer + "\n\n";

    size_t convPos = content.find(":::conversation[", chPos);
    if (convPos != std::string::npos && convPos < chapterEnd) {
        // Append inside existing block before its closing :::
        size_t closePos = content.find("\n:::", convPos);
        if (closePos == std::string::npos || closePos >= chapterEnd) return false;
        content.insert(closePos + 1, newEntry);
    } else {
        // Create new block — insert just before the next chapter section
        std::string block = "\n:::conversation[" + chTitle + "]\n" + newEntry + ":::\n";
        if (nextChPos != std::string::npos) {
            content.insert(nextChPos, block);
        } else {
            if (!content.empty() && content.back() != '\n') content += '\n';
            content += block + '\n';
        }
    }

    std::ofstream fout(filePath);
    if (!fout) return false;
    fout << content;
    return fout.good();
}

// ---------------------------------------------------------------------------
bool DeleteTurn(const std::string& filePath, int chId, int index) {
    std::ifstream fin(filePath);
    if (!fin) return false;
    std::string content((std::istreambuf_iterator<char>(fin)), {});
    fin.close();

    std::string chMarker = "<!-- ch:" + std::to_string(chId) + " -->";
    size_t chPos = content.find(chMarker);
    if (chPos == std::string::npos) return false;

    size_t nextChPos = content.find("\n<!-- ch:", chPos + chMarker.size());
    size_t chapterEnd = (nextChPos == std::string::npos) ? content.size() : nextChPos;

    size_t convPos = content.find(":::conversation[", chPos);
    if (convPos == std::string::npos || convPos >= chapterEnd) return false;

    size_t headerEnd = content.find('\n', convPos);
    if (headerEnd == std::string::npos) return false;
    size_t bodyStart = headerEnd + 1;

    size_t closeNl = content.find("\n:::", convPos);
    if (closeNl == std::string::npos || closeNl >= chapterEnd) return false;

    // body includes up to and including the \n at closeNl
    std::string body = content.substr(bodyStart, closeNl + 1 - bodyStart);
    auto turns = ParseConversation(body);
    if (index < 0 || index >= (int)turns.size()) return false;
    turns.erase(turns.begin() + index);

    if (turns.empty()) {
        // Remove the whole :::conversation block (including the \n before it)
        size_t blockStart = (convPos > 0 && content[convPos - 1] == '\n') ? convPos - 1 : convPos;
        // closeNl points to \n, then ::: (3 chars), then \n
        size_t blockEnd = closeNl + 5;
        if (blockEnd > content.size()) blockEnd = content.size();
        content = content.substr(0, blockStart) + content.substr(blockEnd);
    } else {
        // Replace only the body, keeping the header and closing ::: intact
        std::string newBody = SerializeConversationBody(turns);
        // content.substr(closeNl + 1) starts at the first : of the closing :::
        content = content.substr(0, bodyStart) + newBody + content.substr(closeNl + 1);
    }

    std::ofstream fout(filePath);
    if (!fout) return false;
    fout << content;
    return fout.good();
}

// ---------------------------------------------------------------------------
std::string BuildQAPrompt(const std::string& docMarkdown,
                          const std::string& chTitle,
                          const std::vector<ConversationTurn>& history,
                          const std::string& question) {
    std::ostringstream out;
    out << "## Document\n\n"
        << "The following is the full document the reader is studying:\n\n"
        << "```\n" << docMarkdown << "\n```\n\n"
        << "## Your role\n\n"
        << "You are a knowledgeable and engaging conversation partner. The reader is "
           "currently reading the chapter: **" << chTitle << "**.\n"
        << "Answer their question naturally and helpfully. Use the document above as "
           "context when it is relevant, but do NOT limit yourself to it — draw freely "
           "on your broader knowledge whenever the question calls for it or when the "
           "document does not cover the topic. If the document is silent on something "
           "interesting, say so briefly and then share what you know. Keep answers clear "
           "and conversational — a few sentences to a short paragraph.\n\n";

    if (!history.empty()) {
        out << "## Conversation so far\n\n";
        for (const auto& t : history) {
            out << "Q: " << t.question << "\nA: " << t.answer << "\n\n";
        }
    }

    out << "## New question\n\nQ: " << question << "\nA:";
    return out.str();
}
