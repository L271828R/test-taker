#pragma once
#include <string>
#include <utility>

struct ProjectConfig {
    std::string name;
    std::string llmBackend;   // "claude", "ollama", "api", "clipboard"
    std::string apiKey;
    std::string ollamaModel;
    std::string ollamaUrl;    // default: http://localhost:11434
};

// Creates <baseDir>/<name>/ with a claude.md stub and an empty .index file.
bool CreateProject(const std::string& baseDir, const std::string& name);

// Initialises an existing directory as a project in place (no subdirectory).
// Idempotent — safe to call on a directory that is already a project.
bool InitProject(const std::string& projectDir);

// Returns true when <projectDir>/.index exists.
bool ProjectExists(const std::string& projectDir);

// Read/write the per-project .config key=value file.
ProjectConfig LoadConfig(const std::string& projectDir);
bool SaveConfig(const std::string& projectDir, const ProjectConfig& cfg);

// Registers a chapter file and returns its stable numeric ID.
int RegisterChapter(const std::string& projectDir, const std::string& filename);

// Registers a tidbit at (chapterId, position) and returns its stable numeric ID.
int RegisterTidbit(const std::string& projectDir, int chapterId, int position);

// Returns the filename for chapterId, or "" if not found.
std::string ChapterFile(const std::string& projectDir, int chapterId);

// Returns {chapterId, position} for tidbitId, or {-1,-1} if not found.
std::pair<int, int> TidbitLocation(const std::string& projectDir, int tidbitId);

// Peek the next ID that will be assigned — does NOT consume it.
int NextChapterId(const std::string& projectDir);
int NextTidbitId(const std::string& projectDir);
