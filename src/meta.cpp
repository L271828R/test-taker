#include "meta.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef __APPLE__
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

static std::string metaFilePath(const std::string& projectDir) {
    return projectDir + "/.storyteller.json";
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else                out += c;
    }
    return out;
}

// Extract the value of a JSON string field from a single line.
// Handles both `"key": "val"` and `"key":"val"` spacing, and \" escapes inside values.
static std::string extractStr(const std::string& line, const std::string& key) {
    for (const auto& needle : { "\"" + key + "\": \"", "\"" + key + "\":\"" }) {
        auto pos = line.find(needle);
        if (pos == std::string::npos) continue;
        pos += needle.size();
        std::string val;
        while (pos < line.size()) {
            if (line[pos] == '\\' && pos + 1 < line.size()) {
                char esc = line[pos + 1];
                if      (esc == '"')  { val += '"';  pos += 2; }
                else if (esc == '\\') { val += '\\'; pos += 2; }
                else if (esc == 'n')  { val += '\n'; pos += 2; }
                else                  { val += line[pos++]; }
            } else if (line[pos] == '"') {
                break;
            } else {
                val += line[pos++];
            }
        }
        return val;
    }
    return {};
}

// Extract the value of a JSON integer field from a single line.
static int extractInt(const std::string& line, const std::string& key) {
    for (const auto& needle : { "\"" + key + "\": ", "\"" + key + "\":" }) {
        auto pos = line.find(needle);
        if (pos == std::string::npos) continue;
        pos += needle.size();
        while (pos < line.size() && line[pos] == ' ') ++pos;
        int val = 0;
        while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9')
            val = val * 10 + (line[pos++] - '0');
        return val;
    }
    return 0;
}

// ---------------------------------------------------------------------------
std::string MetaNow() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

static std::string timeToMeta(std::time_t t) {
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

static std::string fallbackCreatedTime(const std::string& projectDir) {
#ifdef __APPLE__
    struct stat st {};
    if (stat(projectDir.c_str(), &st) == 0 && st.st_birthtimespec.tv_sec > 0)
        return timeToMeta(st.st_birthtimespec.tv_sec);
#endif

    std::error_code ec;
    auto ft = fs::last_write_time(projectDir, ec);
    if (ec) return MetaNow();

    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return timeToMeta(std::chrono::system_clock::to_time_t(sctp));
}

// ---------------------------------------------------------------------------
ProjectMeta LoadProjectMeta(const std::string& projectDir) {
    ProjectMeta meta;
    std::ifstream f(metaFilePath(projectDir));
    if (!f) return meta;

    std::string line;
    while (std::getline(f, line)) {
        if (line.find("\"created\"") != std::string::npos) {
            meta.created = extractStr(line, "created");
        } else if (line.find("\"lastOpened\"") != std::string::npos) {
            meta.lastOpened = extractStr(line, "lastOpened");
        } else if (line.find("\"source\"") != std::string::npos) {
            meta.source = extractStr(line, "source");
        }
        // Timing entries are written one per line and contain both "ts" and "op".
        if (line.find("\"ts\"") != std::string::npos &&
            line.find("\"op\"") != std::string::npos) {
            LLMTiming t;
            t.timestamp       = extractStr(line, "ts");
            t.operation       = extractStr(line, "op");
            t.topic           = extractStr(line, "topic");
            t.durationSeconds = extractInt(line, "secs");
            if (!t.timestamp.empty()) meta.timings.push_back(t);
        }
    }
    return meta;
}

// ---------------------------------------------------------------------------
void SaveProjectMeta(const std::string& projectDir, const ProjectMeta& meta) {
    std::ofstream f(metaFilePath(projectDir));
    if (!f) return;

    f << "{\n";
    f << "  \"created\": \"" << jsonEscape(meta.created) << "\",\n";
    f << "  \"lastOpened\": \"" << jsonEscape(meta.lastOpened) << "\",\n";
    f << "  \"source\": \"" << jsonEscape(meta.source) << "\",\n";
    f << "  \"timings\": [\n";
    for (std::size_t i = 0; i < meta.timings.size(); ++i) {
        const auto& t = meta.timings[i];
        f << "    {\"ts\": \""    << jsonEscape(t.timestamp)  << "\", "
          <<  "\"op\": \""        << jsonEscape(t.operation)  << "\", "
          <<  "\"topic\": \""     << jsonEscape(t.topic)      << "\", "
          <<  "\"secs\": "        << t.durationSeconds        << "}";
        if (i + 1 < meta.timings.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
}

// ---------------------------------------------------------------------------
void RecordOpen(const std::string& projectDir) {
    auto meta = LoadProjectMeta(projectDir);
    if (meta.created.empty()) meta.created = fallbackCreatedTime(projectDir);
    meta.lastOpened = MetaNow();
    SaveProjectMeta(projectDir, meta);
}

// ---------------------------------------------------------------------------
void EnsureProjectMeta(const std::string& projectDir, const std::string& source) {
    auto meta = LoadProjectMeta(projectDir);
    bool changed = false;
    if (meta.created.empty()) {
        meta.created = fallbackCreatedTime(projectDir);
        changed = true;
    }
    if (meta.source.empty() && !source.empty()) {
        meta.source = source;
        changed = true;
    }
    if (changed) SaveProjectMeta(projectDir, meta);
}

// ---------------------------------------------------------------------------
void RecordProjectSource(const std::string& projectDir, const std::string& source) {
    auto meta = LoadProjectMeta(projectDir);
    if (meta.created.empty()) meta.created = fallbackCreatedTime(projectDir);
    meta.source = source;
    SaveProjectMeta(projectDir, meta);
}

// ---------------------------------------------------------------------------
void RecordLLMTiming(const std::string& projectDir,
                     const std::string& operation,
                     const std::string& topic,
                     int durationSeconds) {
    auto meta = LoadProjectMeta(projectDir);
    if (meta.created.empty()) meta.created = fallbackCreatedTime(projectDir);
    LLMTiming t;
    t.timestamp       = MetaNow();
    t.operation       = operation;
    t.topic           = topic;
    t.durationSeconds = durationSeconds;
    meta.timings.push_back(std::move(t));
    // Cap at 100 entries to avoid unbounded growth.
    if (meta.timings.size() > 100)
        meta.timings.erase(meta.timings.begin(),
                           meta.timings.begin() + (meta.timings.size() - 100));
    SaveProjectMeta(projectDir, meta);
}
