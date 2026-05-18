#include "saved_convos.h"
#include "markdown.h"
#include <fstream>
#include <sstream>

static std::string savedPath(const std::string& projectDir) {
    return projectDir + "/saved_convos.md";
}

static std::string encodeNL(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else                out += c;
    }
    return out;
}

static std::string decodeNL(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if      (s[i+1] == 'n')  { out += '\n'; ++i; }
            else if (s[i+1] == '\\') { out += '\\'; ++i; }
            else                     { out += s[i]; }
        } else {
            out += s[i];
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
bool AppendSavedConvo(const std::string& projectDir,
                      const std::string& question,
                      const std::string& explanation,
                      const std::string& date) {
    std::ofstream f(savedPath(projectDir), std::ios::app);
    if (!f) return false;
    f << ":::saved\n"
      << "date: " << date << "\n"
      << "Q: "    << encodeNL(question) << "\n"
      << "E: "    << encodeNL(explanation) << "\n"
      << ":::\n\n";
    return f.good();
}

// ---------------------------------------------------------------------------
std::vector<SavedConvo> LoadSavedConvos(const std::string& projectDir) {
    std::ifstream f(savedPath(projectDir));
    if (!f) return {};

    std::vector<SavedConvo> convos;
    SavedConvo cur;
    bool inBlock = false;

    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (!inBlock && line == ":::saved") {
            cur = {};
            inBlock = true;
        } else if (inBlock && line == ":::") {
            if (!cur.question.empty()) convos.push_back(cur);
            inBlock = false;
        } else if (inBlock && line.rfind("date: ", 0) == 0) {
            cur.date = line.substr(6);
        } else if (inBlock && line.rfind("Q: ", 0) == 0) {
            cur.question = decodeNL(line.substr(3));
        } else if (inBlock && line.rfind("E: ", 0) == 0) {
            cur.explanation = decodeNL(line.substr(3));
        }
    }
    return convos;
}

// ---------------------------------------------------------------------------
bool DeleteSavedConvo(const std::string& projectDir, int index) {
    auto convos = LoadSavedConvos(projectDir);
    if (index < 0 || index >= (int)convos.size()) return false;
    convos.erase(convos.begin() + index);

    std::ofstream f(savedPath(projectDir), std::ios::trunc);
    if (!f) return false;
    for (const auto& c : convos) {
        f << ":::saved\n"
          << "date: " << c.date << "\n"
          << "Q: "    << encodeNL(c.question) << "\n"
          << "E: "    << encodeNL(c.explanation) << "\n"
          << ":::\n\n";
    }
    return f.good();
}

// ---------------------------------------------------------------------------
std::string BuildSavedConvosHTML(const std::vector<SavedConvo>& convos) {
    if (convos.empty()) {
        return "<p style='color:var(--text-muted);font-style:italic;'>"
               "No saved conversations yet. Click the \xF0\x9F\x94\x96 icon on any exam turn to save it."
               "</p>";
    }

    std::ostringstream out;
    out << R"(<style>
.saved-entry { position:relative; border-bottom:1px solid var(--border);
               margin-bottom:1.4em; padding-bottom:1em; }
.saved-date { font-size:0.8em; color:var(--text-muted); margin-bottom:0.5em; }
.saved-q { font-weight:600; margin-bottom:0.4em; }
.saved-e { font-size:0.95em; }
.saved-entry:hover .del-btn { opacity:1; }
.del-btn { position:absolute; top:0; right:0;
           opacity:0; transition:opacity 0.15s;
           background:none; border:1px solid var(--border); border-radius:4px;
           padding:0.1em 0.45em; font-size:0.85em; cursor:pointer;
           color:var(--text-muted); text-decoration:none; line-height:1.4; }
.del-btn:hover { color:#cf222e; border-color:#cf222e; }
</style>
)";

    int n = (int)convos.size();
    for (int di = 0; di < n; ++di) {
        int fileIdx = n - 1 - di;           // newest displayed first
        const auto& c = convos[fileIdx];
        out << "<div class='saved-entry'>"
            << "<a class='del-btn' href='testtaker://delete-saved/"
            << fileIdx << "' title='Remove'>&#x2715;</a>"
            << "<div class='saved-date'>" << EscapeHTML(c.date) << "</div>"
            << "<div class='saved-q'>" << RenderMarkdown(c.question) << "</div>"
            << "<div class='saved-e'>" << RenderMarkdown(c.explanation) << "</div>"
            << "</div>\n";
    }
    return out.str();
}
