#include "editor.h"
#include "notes.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <set>

namespace fs = std::filesystem;

static std::string marker_for(int id) {
    return "<!-- tb:" + std::to_string(id) + " -->";
}

static std::string trim_line(std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
        ++start;
    return line.substr(start);
}

static bool is_tidbit_opener(const std::string& line) {
    std::string trimmed = trim_line(line);
    std::size_t colons = 0;
    while (colons < trimmed.size() && trimmed[colons] == ':') ++colons;
    return colons >= 3 && trimmed.compare(colons, 6, "tidbit") == 0;
}

static bool is_tidbit_closer(const std::string& line) {
    std::string trimmed = trim_line(line);
    if (trimmed.size() < 3) return false;
    return trimmed.find_first_not_of(':') == std::string::npos;
}

static std::size_t find_tidbit_opener(const std::string& s, std::size_t start) {
    std::size_t p = start;
    while (p < s.size()) {
        std::size_t nl = s.find('\n', p);
        std::string line = (nl == std::string::npos)
                           ? s.substr(p)
                           : s.substr(p, nl - p);
        if (!trim_line(line).empty()) {
            return is_tidbit_opener(line) ? p : std::string::npos;
        }
        p = (nl == std::string::npos) ? s.size() : nl + 1;
    }
    return std::string::npos;
}

// Returns the position just past the end of the :::tidbit...:::  block
// that starts at `start` in `s`. Returns std::string::npos if not well-formed.
static std::size_t tidbit_end(const std::string& s, std::size_t start) {
    std::size_t p = start;
    bool first_line = true;
    // Skip to the closing fence. Accept indentation and overlong fences because
    // translated LLM output sometimes emits ::::tidbit / ::::.
    while (p < s.size()) {
        std::size_t nl = s.find('\n', p);
        std::string line = (nl == std::string::npos)
                           ? s.substr(p)
                           : s.substr(p, nl - p);
        if (!first_line && is_tidbit_closer(line)) {
            return (nl == std::string::npos) ? s.size() : nl + 1;
        }
        first_line = false;
        p = (nl == std::string::npos) ? s.size() : nl + 1;
    }
    return std::string::npos;
}

std::string ExtractTidbit(const std::string& fileContent, int tidbitId) {
    std::string marker = marker_for(tidbitId);
    auto mpos = fileContent.find(marker);
    if (mpos == std::string::npos) return "";

    // Skip past marker line.
    auto after_marker = fileContent.find('\n', mpos);
    if (after_marker == std::string::npos) return "";
    after_marker += 1;

    // Skip blank lines to find :::tidbit[...].
    std::size_t block_start = find_tidbit_opener(fileContent, after_marker);
    if (block_start == std::string::npos) return "";

    auto block_end = tidbit_end(fileContent, block_start);
    if (block_end == std::string::npos) return "";

    // Trim trailing newline from the returned block.
    std::size_t len = block_end - block_start;
    while (len > 0 && fileContent[block_start + len - 1] == '\n') --len;
    return fileContent.substr(block_start, len);
}

std::string PatchTidbit(const std::string& fileContent,
                        int tidbitId,
                        const std::string& newBlock) {
    std::string marker = marker_for(tidbitId);
    auto mpos = fileContent.find(marker);
    if (mpos == std::string::npos) return fileContent;

    auto after_marker = fileContent.find('\n', mpos);
    if (after_marker == std::string::npos) return fileContent;
    after_marker += 1;

    // Find start of tidbit block (skip blank lines).
    std::size_t block_start = find_tidbit_opener(fileContent, after_marker);
    if (block_start == std::string::npos) return fileContent;

    auto block_end = tidbit_end(fileContent, block_start);
    if (block_end == std::string::npos) return fileContent;

    std::string result;
    result.reserve(fileContent.size());
    result += fileContent.substr(0, block_start);
    result += newBlock;
    result += '\n';
    result += fileContent.substr(block_end);
    return result;
}

bool ApplyTidbitPatch(const std::string& filepath,
                      int tidbitId,
                      const std::string& newBlock) {
    std::string content;
    {
        std::ifstream f(filepath);
        if (!f) return false;
        content.assign(std::istreambuf_iterator<char>(f), {});
    }
    std::string patched = PatchTidbit(content, tidbitId, newBlock);
    std::ofstream f(filepath, std::ios::trunc);
    f << patched;
    return f.good();
}

bool ReplaceChapter(const std::string& filepath, const std::string& newContent) {
    // Rescue any notes whose anchors may have been overwritten by regeneration.
    std::string projectDir = fs::path(filepath).parent_path().string();
    std::string basename   = fs::path(filepath).filename().string();
    auto notes = LoadNotes(projectDir);
    std::string finalContent = RescueOrphanedNotes(newContent, notes, basename);
    SaveNotes(projectDir, notes);

    std::ofstream f(filepath, std::ios::trunc);
    f << finalContent;
    return f.good();
}

static std::string ch_marker_for(int id) {
    return "<!-- ch:" + std::to_string(id) + " -->";
}

std::string ExtractChapter(const std::string& fileContent, int chapterId) {
    std::string marker = ch_marker_for(chapterId);
    auto mpos = fileContent.find(marker);
    if (mpos == std::string::npos) return "";

    // Find the start of the next <!-- ch: --> marker (or EOF).
    auto next = fileContent.find("<!-- ch:", mpos + marker.size());
    std::size_t block_end = (next == std::string::npos) ? fileContent.size() : next;

    // Trim trailing whitespace.
    while (block_end > mpos && fileContent[block_end - 1] == '\n') --block_end;

    return fileContent.substr(mpos, block_end - mpos);
}

bool ApplyChapterPatch(const std::string& filepath,
                       int chapterId,
                       const std::string& newBlock) {
    std::string content;
    {
        std::ifstream f(filepath);
        if (!f) return false;
        content.assign(std::istreambuf_iterator<char>(f), {});
    }

    std::string marker = ch_marker_for(chapterId);
    auto mpos = content.find(marker);
    if (mpos == std::string::npos) return false;

    auto next = content.find("<!-- ch:", mpos + marker.size());
    std::size_t block_end = (next == std::string::npos) ? content.size() : next;

    // If the LLM stripped the marker from its response, re-add it so the
    // splice point survives future rewrites.
    std::string block = newBlock;
    if (block.rfind(marker, 0) != 0)
        block = marker + "\n" + block;

    std::string result;
    result.reserve(content.size());
    result += content.substr(0, mpos);
    result += block;
    result += '\n';
    if (next != std::string::npos)
        result += content.substr(block_end);

    std::ofstream f(filepath, std::ios::trunc);
    f << result;
    return f.good();
}

std::vector<std::string> ApplyFileOrder(const std::vector<std::string>& files,
                                        const std::vector<std::string>& savedOrder) {
    std::set<std::string> present(files.begin(), files.end());
    std::set<std::string> used;
    std::vector<std::string> ordered;

    for (const auto& name : savedOrder) {
        if (present.count(name) && !used.count(name)) {
            ordered.push_back(name);
            used.insert(name);
        }
    }

    std::vector<std::string> remaining;
    for (const auto& name : files) {
        if (!used.count(name)) remaining.push_back(name);
    }
    std::sort(remaining.begin(), remaining.end());
    ordered.insert(ordered.end(), remaining.begin(), remaining.end());
    return ordered;
}

int RefreshedFileSelectionIndex(const std::vector<std::string>& files,
                                const std::string& previousFile) {
    if (files.empty()) return -1;
    auto it = std::find(files.begin(), files.end(), previousFile);
    if (it != files.end())
        return static_cast<int>(std::distance(files.begin(), it));
    return 0;
}

std::vector<std::string> LoadFileOrder(const std::string& projectDir) {
    std::ifstream f(fs::path(projectDir) / ".file_order");
    std::vector<std::string> order;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) order.push_back(line);
    }
    return order;
}

bool SaveFileOrder(const std::string& projectDir,
                   const std::vector<std::string>& files) {
    std::ofstream f(fs::path(projectDir) / ".file_order", std::ios::trunc);
    for (const auto& name : files) f << name << "\n";
    return f.good();
}
