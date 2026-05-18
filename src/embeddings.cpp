#include "embeddings.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::vector<std::string> ChunkText(const std::string& text, int windowWords, int overlapWords) {
    if (text.empty()) return {};

    std::vector<std::string> words;
    {
        std::istringstream iss(text);
        std::string w;
        while (iss >> w) words.push_back(std::move(w));
    }
    if (words.empty()) return {};

    int total = static_cast<int>(words.size());
    if (total <= windowWords) return {text};

    int step = windowWords - overlapWords;
    if (step <= 0) step = 1;

    auto join = [&](int start, int end) {
        std::string out;
        for (int i = start; i < end; ++i) {
            if (i > start) out += ' ';
            out += words[i];
        }
        return out;
    };

    std::vector<std::string> chunks;
    for (int start = 0; start < total; start += step) {
        int end = std::min(start + windowWords, total);
        chunks.push_back(join(start, end));
        if (end == total) break;
    }
    return chunks;
}

static bool IsCodeChunk(const std::string& chunk) {
    // Preprocessor directives are unambiguous code markers
    if (chunk.find("#include") != std::string::npos ||
        chunk.find("#define")  != std::string::npos ||
        chunk.find("#ifndef")  != std::string::npos)
        return true;

    // High semicolon-to-word ratio indicates code (prose rarely uses semicolons)
    int semicolons = 0, words = 0;
    bool inWord = false;
    for (unsigned char c : chunk) {
        if (c == ';') ++semicolons;
        if (std::isspace(c)) { inWord = false; }
        else if (!inWord)    { ++words; inWord = true; }
    }
    return words > 0 && (static_cast<float>(semicolons) / words) > 0.08f;
}

bool IsUsefulChunk(const std::string& chunk, float minAlphaRatio, int minWords) {
    int words = 0, alpha = 0, total = 0;
    bool inWord = false;
    for (unsigned char c : chunk) {
        if (std::isspace(c)) { inWord = false; continue; }
        if (!inWord) { ++words; inWord = true; }
        ++total;
        if (std::isalpha(c)) ++alpha;
    }
    if (words < minWords) return false;
    if (total == 0) return false;
    if (static_cast<float>(alpha) / static_cast<float>(total) < minAlphaRatio) return false;
    if (IsCodeChunk(chunk)) return false;
    return true;
}

// ── Internal helpers ──────────────────────────────────────────────────────────

static std::string temp_path(const std::string& suffix) {
    auto ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() / ("tt_embed_" + std::to_string(ns) + suffix)).string();
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20)  { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
        else                out += static_cast<char>(c);
    }
    return out;
}

static std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
    out += "'";
    return out;
}

// ── Public functions ──────────────────────────────────────────────────────────

EmbedResult EmbedText(const std::string& text,
                       const std::string& ollamaUrl,
                       const std::string& model) {
    std::string jsonFile = temp_path(".json");
    {
        std::ofstream jf(jsonFile);
        jf << "{\"model\":\"" << json_escape(model) << "\","
           << "\"prompt\":\"" << json_escape(text) << "\"}";
    }
    std::string cmd = "curl -s -X POST \"" + ollamaUrl + "/api/embeddings\""
                      " -H 'Content-Type: application/json'"
                      " --data-binary @" + shell_quote(jsonFile) + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        fs::remove(jsonFile);
        return {false, {}, "popen failed"};
    }
    std::string raw;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) raw += buf;
    pclose(pipe);
    fs::remove(jsonFile);

    auto pos = raw.find("\"embedding\"");
    if (pos == std::string::npos)
        return {false, {}, "no embedding field: " + raw.substr(0, 200)};

    auto lb = raw.find('[', pos);
    auto rb = raw.find(']', lb);
    if (lb == std::string::npos || rb == std::string::npos)
        return {false, {}, "malformed embedding array"};

    std::vector<float> vec;
    std::istringstream iss(raw.substr(lb + 1, rb - lb - 1));
    std::string tok;
    while (std::getline(iss, tok, ',')) {
        try { vec.push_back(std::stof(tok)); } catch (...) {}
    }
    if (vec.empty())
        return {false, {}, "empty embedding vector"};

    return {true, std::move(vec), ""};
}

std::string ExtractPDF(const std::string& pdfPath, std::string& err) {
    std::string outFile = temp_path(".txt");
    std::string cmd = "pdftotext " + shell_quote(pdfPath) + " " + shell_quote(outFile) + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { err = "popen failed"; return ""; }
    std::string pdfErr;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) pdfErr += buf;
    int rc = pclose(pipe);
    if (rc != 0) {
        fs::remove(outFile);
        err = "pdftotext failed: " + pdfErr;
        return "";
    }
    std::ifstream f(outFile);
    std::string text(std::istreambuf_iterator<char>(f), {});
    fs::remove(outFile);
    return text;
}
