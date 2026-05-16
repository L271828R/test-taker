#include "llm_response.h"
#include <cctype>

static std::string trim(const std::string& s) {
    const std::string ws = " \t\r\n";
    auto start = s.find_first_not_of(ws);
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

std::string JsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    return out;
}

std::string ExtractJSONString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != ':') return "";
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '"') return "";
    ++pos;

    std::string val;
    bool esc = false;
    for (std::size_t i = pos; i < json.size(); ++i) {
        if (esc) {
            switch (json[i]) {
                case 'n': val += '\n'; break;
                case 't': val += '\t'; break;
                case 'r': val += '\r'; break;
                default:  val += json[i]; break;
            }
            esc = false;
        } else if (json[i] == '\\') {
            esc = true;
        } else if (json[i] == '"') {
            break;
        } else {
            val += json[i];
        }
    }
    return val;
}

std::string ExtractOllamaMarkdown(const std::string& ollamaResponse) {
    std::string response = ExtractJSONString(ollamaResponse, "response");
    if (response.empty()) return "";

    std::string markdown = ExtractJSONString(response, "markdown");
    if (!markdown.empty()) return markdown;

    std::string content = ExtractJSONString(response, "content");
    if (!content.empty()) return content;

    std::string trimmed = trim(response);
    if (!trimmed.empty() && trimmed.front() == '{') {
        return "";
    }
    return response;
}

std::string BuildOllamaStructuredPrompt(const std::string& prompt) {
    return prompt +
        "\n\nReturn exactly one JSON object and no surrounding prose. "
        "Use this schema: {\"markdown\":\"...\"}. "
        "The markdown value must contain the complete Markdown output. "
        "Honor every instruction in the request, especially the requested language. "
        "If the request says 'language: Chinese', the markdown value must be written "
        "in Chinese, not English. Include numbered '## Chapter N: Title' headings and "
        "at least one :::tidbit[...] block in every chapter. Each chapter must contain "
        "at least five complete sentences of story text, not counting tidbit content.";
}

std::vector<std::string> ParseOllamaTags(const std::string& json) {
    std::vector<std::string> names;
    std::size_t pos = 0;
    while (pos < json.size()) {
        auto namePos = json.find("\"name\"", pos);
        if (namePos == std::string::npos) break;
        std::string name = ExtractJSONString(json.substr(namePos), "name");
        pos = namePos + 6;
        if (!name.empty()) names.push_back(name);
    }
    return names;
}
