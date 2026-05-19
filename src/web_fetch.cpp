#include "web_fetch.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <sstream>

// ---------------------------------------------------------------------------
// HTML entity table
static std::string decodeEntities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 10) { out += s[i]; continue; }
        std::string entity = s.substr(i + 1, semi - i - 1);
        if      (entity == "amp")  out += '&';
        else if (entity == "lt")   out += '<';
        else if (entity == "gt")   out += '>';
        else if (entity == "quot") out += '"';
        else if (entity == "apos" || entity == "#39") out += '\'';
        else if (entity == "nbsp") out += ' ';
        else if (!entity.empty() && entity[0] == '#') {
            // Numeric entity &#NNN; or &#xHH;
            int cp = 0;
            if (entity.size() > 1 && entity[1] == 'x')
                cp = std::stoi(entity.substr(2), nullptr, 16);
            else
                cp = std::stoi(entity.substr(1));
            if (cp > 0 && cp < 128)
                out += static_cast<char>(cp);
            else
                out += ' ';
        } else {
            // Unknown entity — pass through
            out += s[i]; continue;
        }
        i = semi;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Remove all occurrences of <tag ...>...</tag> including content (case-insensitive tag match).
static std::string removeBlock(const std::string& s, const std::string& tag) {
    std::string out;
    std::string openTag  = "<" + tag;
    std::string closeTag = "</" + tag + ">";

    // Case-insensitive search helper
    auto ifind = [](const std::string& hay, const std::string& needle, size_t pos) -> size_t {
        if (needle.empty()) return pos;
        for (size_t i = pos; i + needle.size() <= hay.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < needle.size() && match; ++j)
                match = std::tolower((unsigned char)hay[i+j])
                     == std::tolower((unsigned char)needle[j]);
            if (match) return i;
        }
        return std::string::npos;
    };

    size_t pos = 0;
    while (pos < s.size()) {
        size_t start = ifind(s, openTag, pos);
        if (start == std::string::npos) {
            out += s.substr(pos);
            break;
        }
        out += s.substr(pos, start - pos);
        size_t end = ifind(s, closeTag, start);
        if (end == std::string::npos) break;  // malformed — stop
        pos = end + closeTag.size();
    }
    return out;
}

// ---------------------------------------------------------------------------
std::string StripHTML(const std::string& html) {
    std::string s = html;

    // Remove <script> and <style> blocks (with their content).
    s = removeBlock(s, "script");
    s = removeBlock(s, "style");

    // Remove HTML comments <!-- ... -->
    {
        std::string out;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t start = s.find("<!--", pos);
            if (start == std::string::npos) { out += s.substr(pos); break; }
            out += s.substr(pos, start - pos);
            size_t end = s.find("-->", start + 4);
            if (end == std::string::npos) break;
            pos = end + 3;
        }
        s = out;
    }

    // Strip all remaining tags.
    {
        std::string out;
        bool inTag = false;
        for (char c : s) {
            if      (c == '<') inTag = true;
            else if (c == '>') inTag = false;
            else if (!inTag)   out += c;
        }
        s = out;
    }

    // Decode HTML entities.
    s = decodeEntities(s);

    // Collapse runs of whitespace to single spaces / preserve paragraph breaks.
    {
        std::string out;
        bool prevSpace = false;
        bool prevNewline = false;
        for (char c : s) {
            if (c == '\n' || c == '\r') {
                if (!prevNewline) out += '\n';
                prevSpace = true;
                prevNewline = true;
            } else if (c == ' ' || c == '\t') {
                if (!prevSpace) out += ' ';
                prevSpace = true;
            } else {
                out += c;
                prevSpace = false;
                prevNewline = false;
            }
        }
        s = out;
    }

    // Trim leading/trailing whitespace.
    size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last  = s.find_last_not_of(" \t\n\r");
    return s.substr(first, last - first + 1);
}

// ---------------------------------------------------------------------------
std::string NameFromURL(const std::string& url) {
    std::string s = url;

    // Strip scheme
    size_t schemeEnd = s.find("://");
    if (schemeEnd != std::string::npos)
        s = s.substr(schemeEnd + 3);

    // Strip query string and fragment
    size_t q = s.find('?');
    if (q != std::string::npos) s = s.substr(0, q);
    size_t f = s.find('#');
    if (f != std::string::npos) s = s.substr(0, f);

    // Strip common extensions
    for (const char* ext : {".html", ".htm", ".php", ".asp", ".aspx"}) {
        if (s.size() > strlen(ext) &&
            s.substr(s.size() - strlen(ext)) == ext) {
            s = s.substr(0, s.size() - strlen(ext));
            break;
        }
    }

    // Replace non-alphanumeric characters with hyphens
    std::string out;
    for (char c : s) {
        if (std::isalnum((unsigned char)c))
            out += static_cast<char>(std::tolower((unsigned char)c));
        else if (!out.empty() && out.back() != '-')
            out += '-';
    }

    // Strip trailing hyphen
    while (!out.empty() && out.back() == '-')
        out.pop_back();

    // Truncate to 60 characters
    if (out.size() > 60) out = out.substr(0, 60);

    // Strip trailing hyphen again after truncation
    while (!out.empty() && out.back() == '-')
        out.pop_back();

    return out.empty() ? "webpage" : out;
}

// ---------------------------------------------------------------------------
std::string FetchURL(const std::string& url, std::string& err) {
    // Use curl: follow redirects, 30s timeout, max 10MB, fail on HTTP errors.
    std::string cmd = "curl -L -s --max-time 30 --max-filesize 10485760 --fail "
                      "--user-agent 'Mozilla/5.0 (compatible; TestTaker/1.0)' "
                      "\"" + url + "\" 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { err = "popen failed"; return {}; }

    std::string result;
    std::array<char, 4096> buf;
    while (fgets(buf.data(), buf.size(), pipe))
        result += buf.data();

    int status = pclose(pipe);
    if (status != 0) {
        err = "curl exited with status " + std::to_string(status);
        // Return whatever we got so the user can inspect it
        return result;
    }
    return result;
}
