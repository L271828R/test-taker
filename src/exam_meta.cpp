#include "exam_meta.h"
#include "meta.h"   // MetaNow(), fallback time helpers
#include <fstream>
#include <sstream>
#include <algorithm>
#ifdef __APPLE__
#include <sys/stat.h>
#endif

static std::string metaFilePath(const std::string& projectDir) {
    return projectDir + "/.testtaker.json";
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
ExamProjectMeta LoadExamMeta(const std::string& projectDir) {
    ExamProjectMeta meta;
    std::ifstream f(metaFilePath(projectDir));
    if (!f) return meta;

    std::string line;
    SessionRecord cur;
    bool inSession = false;

    while (std::getline(f, line)) {
        if (line.find("\"created\"")    != std::string::npos) meta.created    = extractStr(line, "created");
        if (line.find("\"lastOpened\"") != std::string::npos) meta.lastOpened = extractStr(line, "lastOpened");
        if (line.find("\"llmSource\"")  != std::string::npos) meta.llmSource  = extractStr(line, "llmSource");

        // Session records: each record starts with "file" field
        if (line.find("\"file\"") != std::string::npos) {
            if (inSession) meta.sessions.push_back(cur);
            cur       = {};
            inSession = true;
            cur.sessionFile = extractStr(line, "file");
        }
        if (inSession) {
            if (line.find("\"startedAt\"")  != std::string::npos) cur.startedAt      = extractStr(line, "startedAt");
            if (line.find("\"finishedAt\"") != std::string::npos) cur.finishedAt     = extractStr(line, "finishedAt");
            if (line.find("\"topic\"")      != std::string::npos) cur.topic          = extractStr(line, "topic");
            if (line.find("\"difficulty\"") != std::string::npos) cur.difficulty     = extractStr(line, "difficulty");
            if (line.find("\"total\"")      != std::string::npos) cur.totalQuestions = extractInt(line, "total");
            if (line.find("\"correct\"")    != std::string::npos) cur.correct        = extractInt(line, "correct");
            if (line.find("\"partial\"")    != std::string::npos) cur.partial        = extractInt(line, "partial");
            if (line.find("\"missed\"")     != std::string::npos) cur.missed         = extractInt(line, "missed");
            if (line.find("\"skipped\"")    != std::string::npos) cur.skipped        = extractInt(line, "skipped");
            if (line.find("\"flagged\"")    != std::string::npos) cur.flaggedCount   = extractInt(line, "flagged");
            // End of object — flush on closing brace line
            if (line.find('}') != std::string::npos && line.find("\"file\"") == std::string::npos) {
                meta.sessions.push_back(cur);
                cur       = {};
                inSession = false;
            }
        }
    }
    return meta;
}

// ---------------------------------------------------------------------------
void SaveExamMeta(const std::string& projectDir, const ExamProjectMeta& meta) {
    std::ofstream f(metaFilePath(projectDir));
    if (!f) return;

    f << "{\n";
    f << "  \"created\": \""    << jsonEscape(meta.created)    << "\",\n";
    f << "  \"lastOpened\": \"" << jsonEscape(meta.lastOpened) << "\",\n";
    f << "  \"llmSource\": \""  << jsonEscape(meta.llmSource)  << "\",\n";
    f << "  \"sessions\": [\n";
    for (std::size_t i = 0; i < meta.sessions.size(); ++i) {
        const auto& s = meta.sessions[i];
        f << "    {\n";
        f << "      \"file\": \""       << jsonEscape(s.sessionFile) << "\",\n";
        f << "      \"startedAt\": \""  << jsonEscape(s.startedAt)   << "\",\n";
        f << "      \"finishedAt\": \"" << jsonEscape(s.finishedAt)  << "\",\n";
        f << "      \"topic\": \""      << jsonEscape(s.topic)       << "\",\n";
        f << "      \"difficulty\": \"" << jsonEscape(s.difficulty)  << "\",\n";
        f << "      \"total\": "        << s.totalQuestions          << ",\n";
        f << "      \"correct\": "      << s.correct                 << ",\n";
        f << "      \"partial\": "      << s.partial                 << ",\n";
        f << "      \"missed\": "       << s.missed                  << ",\n";
        f << "      \"skipped\": "      << s.skipped                 << ",\n";
        f << "      \"flagged\": "      << s.flaggedCount            << "\n";
        f << "    }";
        if (i + 1 < meta.sessions.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
}

// ---------------------------------------------------------------------------
void RecordSession(const std::string& projectDir, const SessionRecord& rec) {
    auto meta = LoadExamMeta(projectDir);
    if (meta.created.empty()) meta.created = MetaNow();

    SessionRecord normalized = rec;
    auto nl = normalized.topic.find('\n');
    if (nl != std::string::npos)
        normalized.topic = normalized.topic.substr(0, nl);
    while (!normalized.topic.empty() && normalized.topic.back() == ' ')
        normalized.topic.pop_back();

    auto it = std::find_if(meta.sessions.begin(), meta.sessions.end(),
        [&](const SessionRecord& s){ return s.sessionFile == normalized.sessionFile; });
    if (it != meta.sessions.end())
        *it = normalized;
    else
        meta.sessions.push_back(normalized);

    SaveExamMeta(projectDir, meta);
}

// ---------------------------------------------------------------------------
void RecordExamOpen(const std::string& projectDir) {
    auto meta = LoadExamMeta(projectDir);
    if (meta.created.empty()) meta.created = MetaNow();
    meta.lastOpened = MetaNow();
    SaveExamMeta(projectDir, meta);
}

// ---------------------------------------------------------------------------
void EnsureExamMeta(const std::string& projectDir, const std::string& llmSource) {
    auto meta = LoadExamMeta(projectDir);
    bool changed = false;
    if (meta.created.empty()) {
        meta.created = MetaNow();
        changed = true;
    }
    if (meta.llmSource.empty() && !llmSource.empty()) {
        meta.llmSource = llmSource;
        changed = true;
    }
    if (changed) SaveExamMeta(projectDir, meta);
}
