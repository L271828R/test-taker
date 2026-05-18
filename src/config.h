#pragma once
#include <string>

struct AppConfig {
    std::string defaultFolder;
    std::string defaultPrompt;
};

// Parse config content from a string (testable without filesystem access).
AppConfig ParseConfig(const std::string& content);

// Read ~/.config/test-taker/config and return the parsed result.
// Returns default-constructed AppConfig if the file does not exist.
AppConfig LoadConfig();

// ── App state — persisted across sessions ─────────────────────────────────────
struct AppState {
    std::string currentProject; // folder name within defaultFolder, e.g. "my-story"

    // Create tab form state
    std::string topic;
    std::string instructions;
    std::string style;
    std::string backend;
    std::string checkedChars; // pipe-separated, e.g. "Einstein|Curie"

    // Backend credentials (not secret — stored in plain-text state file)
    std::string apiKey;
    std::string ollamaModel;

    // Last active session file (basename only); kept even when session is complete
    // so the Exam tab can display completed turns read-only on next startup.
    std::string lastSessionFile;

    // Focus-area list serialized as "stars@@text|stars@@text|..."
    std::string focusAreas;

    // Project dir that was active when the New Session form was last saved.
    // Used to detect whether the project changed on restart (avoids clearing
    // topic/focus areas when reopening the same project).
    std::string lastExamProjectDir;
};

AppState ParseState(const std::string& content);
AppState LoadAppState();
void     SaveAppState(const AppState& state);
