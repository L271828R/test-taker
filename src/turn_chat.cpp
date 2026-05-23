#include "turn_chat.h"
#include "html_template.h"
#include "markdown.h"
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
std::vector<TurnChatTurn> ParseTurnChat(const std::string& body) {
    std::vector<TurnChatTurn> turns;
    TurnChatTurn cur;
    bool inAnswer = false;

    auto pushCur = [&]() {
        if (cur.question.empty()) return;
        // Trim trailing newlines from the answer before storing.
        while (!cur.answer.empty() && cur.answer.back() == '\n')
            cur.answer.pop_back();
        turns.push_back(cur);
        cur = {};
    };

    std::istringstream ss(body);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.rfind("Q: ", 0) == 0) {
            pushCur();
            cur = {line.substr(3), ""};
            inAnswer = false;
        } else if (line.rfind("A: ", 0) == 0) {
            cur.answer = line.substr(3);
            inAnswer = true;
        } else if (inAnswer) {
            // Preserve blank lines as paragraph separators within the answer.
            cur.answer += "\n" + line;
        }
    }
    pushCur();
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
std::string BuildTurnChatHTML(const QuestionTurn& examTurn,
                               int turnIndex,
                               const std::vector<TurnChatTurn>& turns,
                               bool darkMode,
                               const std::set<int>& savedIndices,
                               const std::string& pendingQ) {
    std::ostringstream body;

    body << R"(<style>
body { padding: 12px; }
.turn { margin-bottom:16px; }
.turn-toolbar { display:flex; gap:0.4em; margin-bottom:0.3em; }
.turn:hover .tc-save-btn { opacity:1; }
.tc-save-btn { opacity:0; transition:opacity 0.15s;
  background:none; border:1px solid var(--border); border-radius:4px;
  padding:0.15em 0.5em; font-size:0.82em; cursor:pointer;
  color:var(--text-muted); text-decoration:none; white-space:nowrap; }
.tc-save-btn.saved { color:#1a7f37; border-color:#1a7f37; opacity:1; }
.q { background:var(--surface); border:1px solid var(--border);
     border-radius:8px 8px 8px 2px; padding:8px 12px;
     margin-bottom:6px; font-weight:500; }
.a { border-radius:2px 8px 8px 8px; padding:8px 12px; }
.a p:last-child { margin-bottom:0; }
.a pre { font-size:85%; }
.thinking { color:var(--text-muted); font-style:italic; padding:8px 12px; }
.empty { color:var(--text-muted); font-style:italic; }
</style>
)";

    if (turnIndex < 0) {
        body << "<p class='empty'>Click <strong>&#x1F4AC; discuss</strong> on any "
                "completed question to start a follow-up conversation.</p>";
    } else {
        for (int i = 0; i < (int)turns.size(); ++i) {
            const auto& t = turns[i];
            bool isSaved = savedIndices.count(i) > 0;
            std::string saveClass = isSaved ? " saved" : "";
            std::string saveLabel = isSaved ? "&#x1F516; saved" : "&#x1F516; save";
            body << "<div class='turn'>"
                 << "<div class='turn-toolbar'>"
                 << "<a class='tc-save-btn" << saveClass << "' href='testtaker://tc-save/"
                 << i << "'>" << saveLabel << "</a>"
                 << "</div>"
                 << "<div class='q'>" << RenderMarkdown(t.question) << "</div>"
                 << "<div class='a'>" << RenderMarkdown(t.answer) << "</div>"
                 << "</div>\n";
        }
        if (!pendingQ.empty()) {
            body << "<div class='turn'>"
                 << "<div class='q'>" << RenderMarkdown(pendingQ) << "</div>"
                 << "<div class='thinking'>&#x22EF;</div>"
                 << "</div>\n";
        }
        if (turns.empty() && pendingQ.empty()) {
            body << "<p class='empty'>Ask a follow-up question about this result.</p>";
        }
    }

    body << "<script>window.scrollTo(0,document.body.scrollHeight);</script>";

    return BuildHTML(body.str(), "Discussion", darkMode);
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
