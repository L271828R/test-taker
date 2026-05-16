#include "notes.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Minimal hand-rolled JSON helpers
// ---------------------------------------------------------------------------

// Escape a string value for JSON output.
static std::string JsonEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 4);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:   r += static_cast<char>(c);
        }
    }
    return r;
}

// Decode a JSON string value (called after the opening '"' is consumed).
// src[pos] must point to the character after the opening quote.
// Advances pos past the closing '"'. Returns the decoded string.
static std::string JsonDecodeString(const std::string& src, size_t& pos) {
    std::string r;
    while (pos < src.size()) {
        char c = src[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < src.size()) {
            char esc = src[pos++];
            switch (esc) {
                case '"':  r += '"';  break;
                case '\\': r += '\\'; break;
                case 'n':  r += '\n'; break;
                case 'r':  r += '\r'; break;
                case 't':  r += '\t'; break;
                default:   r += esc;  break;
            }
        } else {
            r += c;
        }
    }
    return r;
}

// Skip whitespace in src from pos.
static void SkipWS(const std::string& src, size_t& pos) {
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' ||
                                src[pos] == '\n' || src[pos] == '\r'))
        ++pos;
}

// Parse notes.json — a flat JSON array of objects with fields:
//   id (integer), anchor, selectedText, text, file (all strings).
// Returns parsed notes; returns empty on any parse error.
static std::vector<Note> ParseNotesJson(const std::string& json) {
    std::vector<Note> result;
    size_t pos = 0;
    SkipWS(json, pos);
    if (pos >= json.size() || json[pos] != '[') return result;
    ++pos; // consume '['

    while (true) {
        SkipWS(json, pos);
        if (pos >= json.size()) break;
        if (json[pos] == ']') break;
        if (json[pos] == ',') { ++pos; continue; }
        if (json[pos] != '{') break;
        ++pos; // consume '{'

        Note n;
        n.id = 0;
        while (true) {
            SkipWS(json, pos);
            if (pos >= json.size()) break;
            if (json[pos] == '}') { ++pos; break; }
            if (json[pos] == ',') { ++pos; continue; }
            if (json[pos] != '"') { ++pos; continue; }
            ++pos; // consume opening '"'
            std::string key = JsonDecodeString(json, pos);
            SkipWS(json, pos);
            if (pos >= json.size() || json[pos] != ':') continue;
            ++pos; // consume ':'
            SkipWS(json, pos);
            if (pos >= json.size()) break;
            if (json[pos] == '"') {
                ++pos;
                std::string val = JsonDecodeString(json, pos);
                if      (key == "anchor")       n.anchor       = val;
                else if (key == "selectedText")  n.selectedText = val;
                else if (key == "text")          n.text         = val;
                else if (key == "file")          n.file         = val;
            } else {
                // numeric
                size_t numStart = pos;
                while (pos < json.size() && json[pos] != ',' && json[pos] != '}' &&
                       json[pos] != ']' && json[pos] != ' ' && json[pos] != '\n')
                    ++pos;
                std::string numStr = json.substr(numStart, pos - numStart);
                if (key == "id") {
                    try { n.id = std::stoi(numStr); } catch (...) {}
                }
            }
        }
        result.push_back(n);
    }
    return result;
}

// ---------------------------------------------------------------------------
// LoadNotes / SaveNotes
// ---------------------------------------------------------------------------

std::vector<Note> LoadNotes(const std::string& projectDir) {
    fs::path p = fs::path(projectDir) / "notes.json";
    std::ifstream f(p);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ParseNotesJson(ss.str());
}

bool SaveNotes(const std::string& projectDir, const std::vector<Note>& notes) {
    fs::path p = fs::path(projectDir) / "notes.json";
    std::ofstream f(p, std::ios::trunc);
    if (!f) return false;
    f << "[\n";
    for (size_t i = 0; i < notes.size(); ++i) {
        const Note& n = notes[i];
        f << "  {"
          << "\"id\":" << n.id << ","
          << "\"anchor\":\"" << JsonEscape(n.anchor) << "\","
          << "\"selectedText\":\"" << JsonEscape(n.selectedText) << "\","
          << "\"text\":\"" << JsonEscape(n.text) << "\","
          << "\"file\":\"" << JsonEscape(n.file) << "\""
          << "}";
        if (i + 1 < notes.size()) f << ",";
        f << "\n";
    }
    f << "]\n";
    return f.good();
}

// ---------------------------------------------------------------------------
// NextNoteId
// ---------------------------------------------------------------------------

int NextNoteId(const std::vector<Note>& notes) {
    if (notes.empty()) return 1;
    int maxId = 0;
    for (const auto& n : notes)
        if (n.id > maxId) maxId = n.id;
    return maxId + 1;
}

// ---------------------------------------------------------------------------
// InsertNoteAnchor
// ---------------------------------------------------------------------------

std::string InsertNoteAnchor(const std::string& mdContent,
                             const std::string& selectedText,
                             const std::string& context,
                             int noteId) {
    const std::string marker = "<!-- note:" + std::to_string(noteId) + " -->";

    // Strategy 1: find context in mdContent, then selectedText within that region.
    if (!context.empty()) {
        size_t ctxPos = mdContent.find(context);
        if (ctxPos != std::string::npos) {
            size_t textPos = mdContent.find(selectedText, ctxPos);
            if (textPos != std::string::npos &&
                textPos < ctxPos + context.size() + selectedText.size()) {
                size_t insertAt = textPos + selectedText.size();
                return mdContent.substr(0, insertAt) + marker + mdContent.substr(insertAt);
            }
        }
    }

    // Strategy 2: fall back to first occurrence of selectedText directly.
    size_t textPos = mdContent.find(selectedText);
    if (textPos == std::string::npos) return mdContent;
    size_t insertAt = textPos + selectedText.size();
    return mdContent.substr(0, insertAt) + marker + mdContent.substr(insertAt);
}

// ---------------------------------------------------------------------------
// RemoveNoteAnchor
// ---------------------------------------------------------------------------

std::string RemoveNoteAnchor(const std::string& mdContent, int noteId) {
    const std::string marker = "<!-- note:" + std::to_string(noteId) + " -->";
    std::string result = mdContent;
    size_t pos = 0;
    while ((pos = result.find(marker, pos)) != std::string::npos) {
        result.erase(pos, marker.size());
    }
    return result;
}

// ---------------------------------------------------------------------------
// RescueOrphanedNotes
// ---------------------------------------------------------------------------

std::string RescueOrphanedNotes(const std::string& mdContent,
                                std::vector<Note>& notes,
                                const std::string& mdBasename) {
    // Scan for all <!-- note:N --> patterns.
    std::vector<int> foundIds;
    const std::string prefix = "<!-- note:";
    const std::string suffix = " -->";
    size_t pos = 0;
    while ((pos = mdContent.find(prefix, pos)) != std::string::npos) {
        size_t numStart = pos + prefix.size();
        size_t numEnd   = mdContent.find(suffix, numStart);
        if (numEnd == std::string::npos) { ++pos; continue; }
        std::string numStr = mdContent.substr(numStart, numEnd - numStart);
        try {
            int id = std::stoi(numStr);
            foundIds.push_back(id);
        } catch (...) {}
        pos = numEnd + suffix.size();
    }

    if (foundIds.empty()) return mdContent;

    // Gather rescued note texts.
    struct Rescued { int id; std::string sel; std::string text; };
    std::vector<Rescued> rescued;
    for (int id : foundIds) {
        for (const auto& n : notes) {
            if (n.id == id && (n.file.empty() || n.file == mdBasename)) {
                rescued.push_back({id, n.selectedText, n.text});
                break;
            }
        }
    }

    // Strip ALL <!-- note:N --> markers from the content.
    std::string stripped = mdContent;
    {
        size_t p = 0;
        while ((p = stripped.find(prefix, p)) != std::string::npos) {
            size_t end = stripped.find(suffix, p + prefix.size());
            if (end == std::string::npos) { ++p; continue; }
            size_t removeEnd = end + suffix.size();
            stripped.erase(p, removeEnd - p);
        }
    }

    // Append rescued notes section.
    if (!rescued.empty()) {
        stripped += "\n## Notes (from previous version)\n\n";
        for (const auto& r : rescued) {
            stripped += "- **[note on: \"" + r.sel + "\"]** " + r.text + "\n";
        }
    }

    // Remove rescued notes from the notes vector.
    std::vector<int> rescuedIds;
    for (const auto& r : rescued) rescuedIds.push_back(r.id);
    notes.erase(
        std::remove_if(notes.begin(), notes.end(), [&](const Note& n) {
            return std::find(rescuedIds.begin(), rescuedIds.end(), n.id) != rescuedIds.end();
        }),
        notes.end()
    );

    return stripped;
}
