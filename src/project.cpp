#include "project.h"
#include "git_ops.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace fs = std::filesystem;

static fs::path IndexPath(const std::string& projectDir) {
    return fs::path(projectDir) / ".index";
}

static fs::path ConfigPath(const std::string& projectDir) {
    return fs::path(projectDir) / ".config";
}

static std::map<std::string, std::string> ReadIndex(const std::string& projectDir) {
    std::map<std::string, std::string> m;
    std::ifstream f(IndexPath(projectDir));
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        m[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return m;
}

static bool WriteIndex(const std::string& projectDir,
                       const std::map<std::string, std::string>& m) {
    std::ofstream f(IndexPath(projectDir));
    for (auto& [k, v] : m)
        f << k << "=" << v << "\n";
    return f.good();
}

static int ReadInt(const std::map<std::string, std::string>& m,
                   const std::string& key, int fallback) {
    auto it = m.find(key);
    if (it == m.end()) return fallback;
    try { return std::stoi(it->second); } catch (...) { return fallback; }
}

static bool write_stub_files(const fs::path& proj, const std::string& name) {
    if (!fs::exists(proj / "claude.md")) {
        std::ofstream md(proj / "claude.md");
        md << "# " << name << "\n\n"
           << "Describe the exam topic and any context that should be included "
           << "in every question. For example: difficulty level, specific subtopics "
           << "to focus on, relevant background knowledge, or reference material.\n";
        if (!md.good()) return false;
    }
    if (!fs::exists(proj / ".index")) {
        std::ofstream idx(proj / ".index");
        idx << "next_ch=1\nnext_tb=1\n";
        if (!idx.good()) return false;
    }
    return true;
}

bool CreateProject(const std::string& baseDir, const std::string& name) {
    fs::path proj = fs::path(baseDir) / name;
    std::error_code ec;
    fs::create_directories(proj, ec);
    if (ec && !fs::exists(proj)) return false;
    if (!write_stub_files(proj, name)) return false;
    GitInit(proj.string());
    return true;
}

bool InitProject(const std::string& projectDir) {
    fs::path proj(projectDir);
    std::error_code ec;
    fs::create_directories(proj, ec);
    if (ec && !fs::exists(proj)) return false;
    std::string name = proj.filename().string();
    return write_stub_files(proj, name);
}

bool ProjectExists(const std::string& projectDir) {
    return fs::exists(IndexPath(projectDir));
}

ProjectConfig LoadConfig(const std::string& projectDir) {
    ProjectConfig cfg;
    std::ifstream f(ConfigPath(projectDir));
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        if      (k == "name")             cfg.name             = v;
        else if (k == "llmBackend")       cfg.llmBackend       = v;
        else if (k == "apiKey")           cfg.apiKey           = v;
        else if (k == "ollamaModel")      cfg.ollamaModel      = v;
        else if (k == "ollamaUrl")        cfg.ollamaUrl        = v;
        else if (k == "personalities")    cfg.personalities    = v;
        else if (k == "examTopic")        cfg.examTopic        = v;
        else if (k == "examInstructions") cfg.examInstructions = v;
        else if (k == "examFocusAreas")   cfg.examFocusAreas   = v;
        else if (k == "examMoreOf")       cfg.examMoreOf       = v;
        else if (k == "examLessOf")       cfg.examLessOf       = v;
        else if (k == "examTidbitCount")  cfg.examTidbitCount  = std::stoi(v.empty() ? "1" : v);
        else if (k == "examBackend")      cfg.examBackend      = v;
        else if (k == "examApiKey")       cfg.examApiKey       = v;
        else if (k == "examOllamaModel")  cfg.examOllamaModel  = v;
        else if (k == "lastSession")      cfg.lastSession      = v;
    }
    return cfg;
}

bool SaveConfig(const std::string& projectDir, const ProjectConfig& cfg) {
    std::ofstream f(ConfigPath(projectDir));
    f << "name="             << cfg.name             << "\n"
      << "llmBackend="       << cfg.llmBackend       << "\n"
      << "apiKey="           << cfg.apiKey           << "\n"
      << "ollamaModel="      << cfg.ollamaModel      << "\n"
      << "ollamaUrl="        << cfg.ollamaUrl        << "\n"
      << "personalities="    << cfg.personalities    << "\n"
      << "examTopic="        << cfg.examTopic        << "\n"
      << "examInstructions=" << cfg.examInstructions << "\n"
      << "examFocusAreas="   << cfg.examFocusAreas   << "\n"
      << "examMoreOf="       << cfg.examMoreOf       << "\n"
      << "examLessOf="       << cfg.examLessOf       << "\n"
      << "examTidbitCount="  << cfg.examTidbitCount  << "\n"
      << "examBackend="      << cfg.examBackend      << "\n"
      << "examApiKey="       << cfg.examApiKey       << "\n"
      << "examOllamaModel="  << cfg.examOllamaModel  << "\n"
      << "lastSession="      << cfg.lastSession      << "\n";
    return f.good();
}

int RegisterChapter(const std::string& projectDir, const std::string& filename) {
    auto m  = ReadIndex(projectDir);
    int  id = ReadInt(m, "next_ch", 1);
    m["ch:" + std::to_string(id)] = filename;
    m["next_ch"] = std::to_string(id + 1);
    WriteIndex(projectDir, m);
    return id;
}

int RegisterTidbit(const std::string& projectDir, int chapterId, int position) {
    auto m  = ReadIndex(projectDir);
    int  id = ReadInt(m, "next_tb", 1);
    m["tb:" + std::to_string(id)] =
        std::to_string(chapterId) + ":" + std::to_string(position);
    m["next_tb"] = std::to_string(id + 1);
    WriteIndex(projectDir, m);
    return id;
}

std::string ChapterFile(const std::string& projectDir, int chapterId) {
    auto m  = ReadIndex(projectDir);
    auto it = m.find("ch:" + std::to_string(chapterId));
    return it != m.end() ? it->second : "";
}

int NextChapterId(const std::string& projectDir) {
    return ReadInt(ReadIndex(projectDir), "next_ch", 1);
}

int NextTidbitId(const std::string& projectDir) {
    return ReadInt(ReadIndex(projectDir), "next_tb", 1);
}

bool IsProjectDeletable(const std::string& projectPath, const std::string& defaultFolder) {
    if (projectPath.empty() || defaultFolder.empty()) return false;
    if (projectPath == defaultFolder) return false;
    std::string prefix = defaultFolder;
    if (prefix.back() != '/') prefix += '/';
    return projectPath.rfind(prefix, 0) == 0;
}

std::pair<int, int> TidbitLocation(const std::string& projectDir, int tidbitId) {
    auto m  = ReadIndex(projectDir);
    auto it = m.find("tb:" + std::to_string(tidbitId));
    if (it == m.end()) return {-1, -1};
    auto colon = it->second.find(':');
    if (colon == std::string::npos) return {-1, -1};
    try {
        int chId = std::stoi(it->second.substr(0, colon));
        int pos  = std::stoi(it->second.substr(colon + 1));
        return {chId, pos};
    } catch (...) { return {-1, -1}; }
}
