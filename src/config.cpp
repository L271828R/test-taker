#include "config.h"
#include <fstream>
#include <sstream>
#include <cstdlib>

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r");
    size_t end   = s.find_last_not_of(" \t\r");
    return start == std::string::npos ? "" : s.substr(start, end - start + 1);
}

AppConfig ParseConfig(const std::string& content) {
    AppConfig cfg;
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(trimmed.substr(0, eq));
        std::string val = trim(trimmed.substr(eq + 1));
        if      (key == "defaultFolder") cfg.defaultFolder = val;
        else if (key == "defaultPrompt") cfg.defaultPrompt = val;
    }
    return cfg;
}

AppConfig LoadConfig() {
    const char* home = getenv("HOME");
    if (!home) return {};
    std::string path = std::string(home) + "/.config/test-taker/config";
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ParseConfig(ss.str());
}

// ── App state ─────────────────────────────────────────────────────────────────

static std::string StatePath() {
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/.config/test-taker/state" : "";
}

AppState ParseState(const std::string& content) {
    AppState st;
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(trimmed.substr(0, eq));
        std::string val = trim(trimmed.substr(eq + 1));
        if      (key == "currentProject") st.currentProject = val;
        else if (key == "topic")          st.topic          = val;
        else if (key == "instructions") {
            // decode \\n back to newlines
            std::string decoded;
            for (std::size_t i = 0; i < val.size(); ++i) {
                if (val[i] == '\\' && i + 1 < val.size() && val[i+1] == 'n') {
                    decoded += '\n'; ++i;
                } else {
                    decoded += val[i];
                }
            }
            st.instructions = decoded;
        }
        else if (key == "style")          st.style          = val;
        else if (key == "backend")        st.backend        = val;
        else if (key == "checkedChars")   st.checkedChars   = val;
        else if (key == "apiKey")         st.apiKey            = val;
        else if (key == "ollamaModel")    st.ollamaModel       = val;
        else if (key == "lastSessionFile")    st.lastSessionFile    = val;
        else if (key == "focusAreas")         st.focusAreas         = val;
        else if (key == "lastExamProjectDir") st.lastExamProjectDir = val;
    }
    return st;
}

AppState LoadAppState() {
    std::string path = StatePath();
    if (path.empty()) return {};
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ParseState(ss.str());
}

void SaveAppState(const AppState& state) {
    std::string path = StatePath();
    if (path.empty()) return;
    std::ofstream f(path);
    // encode newlines in instructions as \n for the single-line state file
    std::string encodedInstr;
    for (char c : state.instructions)
        encodedInstr += (c == '\n') ? "\\n" : std::string(1, c);

    f << "currentProject = "   << state.currentProject   << "\n"
      << "topic = "            << state.topic            << "\n"
      << "instructions = "     << encodedInstr           << "\n"
      << "style = "            << state.style            << "\n"
      << "backend = "          << state.backend          << "\n"
      << "checkedChars = "     << state.checkedChars     << "\n"
      << "apiKey = "           << state.apiKey           << "\n"
      << "ollamaModel = "      << state.ollamaModel      << "\n"
      << "lastSessionFile = "    << state.lastSessionFile    << "\n"
      << "focusAreas = "         << state.focusAreas         << "\n"
      << "lastExamProjectDir = " << state.lastExamProjectDir << "\n";
}
