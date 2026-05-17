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

    // Last active session file (basename only); empty when session completed.
    std::string lastSessionFile;
};

AppState ParseState(const std::string& content);
AppState LoadAppState();
void     SaveAppState(const AppState& state);
