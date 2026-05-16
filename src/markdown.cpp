#include "markdown.h"
#include <map>
#include <sstream>
#include <vector>
#include <cctype>
#include <algorithm>

std::string EscapeHTML(const std::string& text) {
    std::string r;
    r.reserve(text.size() + 16);
    for (unsigned char c : text) {
        switch (c) {
            case '&':  r += "&amp;";  break;
            case '<':  r += "&lt;";   break;
            case '>':  r += "&gt;";   break;
            case '"':  r += "&quot;"; break;
            default:   r += static_cast<char>(c);
        }
    }
    return r;
}

std::string ProcessInline(const std::string& text) {
    std::string r;
    r.reserve(text.size() * 2);
    size_t i = 0;
    const size_t n = text.size();

    while (i < n) {
        char c = text[i];

        // Inline code  `…`
        if (c == '`') {
            size_t end = text.find('`', i + 1);
            if (end != std::string::npos) {
                r += "<code>" + EscapeHTML(text.substr(i + 1, end - i - 1)) + "</code>";
                i = end + 1;
                continue;
            }
        }

        // Bold+italic  ***…***
        if (c == '*' && i + 2 < n && text[i+1] == '*' && text[i+2] == '*') {
            size_t end = text.find("***", i + 3);
            if (end != std::string::npos) {
                r += "<strong><em>" + ProcessInline(text.substr(i + 3, end - i - 3)) + "</em></strong>";
                i = end + 3;
                continue;
            }
        }

        // Bold  **…**
        if (c == '*' && i + 1 < n && text[i+1] == '*') {
            size_t end = text.find("**", i + 2);
            if (end != std::string::npos) {
                r += "<strong>" + ProcessInline(text.substr(i + 2, end - i - 2)) + "</strong>";
                i = end + 2;
                continue;
            }
        }

        // Bold  __…__
        if (c == '_' && i + 1 < n && text[i+1] == '_') {
            size_t end = text.find("__", i + 2);
            if (end != std::string::npos) {
                r += "<strong>" + ProcessInline(text.substr(i + 2, end - i - 2)) + "</strong>";
                i = end + 2;
                continue;
            }
        }

        // Italic  *…*
        if (c == '*') {
            size_t end = text.find('*', i + 1);
            if (end != std::string::npos) {
                r += "<em>" + ProcessInline(text.substr(i + 1, end - i - 1)) + "</em>";
                i = end + 1;
                continue;
            }
        }

        // Italic  _…_
        if (c == '_') {
            size_t end = text.find('_', i + 1);
            if (end != std::string::npos) {
                r += "<em>" + ProcessInline(text.substr(i + 1, end - i - 1)) + "</em>";
                i = end + 1;
                continue;
            }
        }

        // Strikethrough  ~~…~~
        if (c == '~' && i + 1 < n && text[i+1] == '~') {
            size_t end = text.find("~~", i + 2);
            if (end != std::string::npos) {
                r += "<del>" + ProcessInline(text.substr(i + 2, end - i - 2)) + "</del>";
                i = end + 2;
                continue;
            }
        }

        // Image  ![alt](url)
        if (c == '!' && i + 1 < n && text[i+1] == '[') {
            size_t aS = i + 2;
            size_t aE = text.find(']', aS);
            if (aE != std::string::npos && aE + 1 < n && text[aE+1] == '(') {
                size_t uS = aE + 2;
                size_t uE = text.find(')', uS);
                if (uE != std::string::npos) {
                    std::string alt = text.substr(aS, aE - aS);
                    std::string url = text.substr(uS, uE - uS);
                    r += "<img src=\"" + url + "\" alt=\"" + EscapeHTML(alt) + "\" loading=\"lazy\">";
                    i = uE + 1;
                    continue;
                }
            }
        }

        // Link  [text](url)
        if (c == '[') {
            size_t tS = i + 1;
            size_t tE = text.find(']', tS);
            if (tE != std::string::npos && tE + 1 < n && text[tE+1] == '(') {
                size_t uS = tE + 2;
                size_t uE = text.find(')', uS);
                if (uE != std::string::npos) {
                    std::string ltext = text.substr(tS, tE - tS);
                    std::string url   = text.substr(uS, uE - uS);
                    r += "<a href=\"" + url + "\">" + ProcessInline(ltext) + "</a>";
                    i = uE + 1;
                    continue;
                }
            }
        }

        // Auto-link  <url>
        if (c == '<') {
            size_t end = text.find('>', i + 1);
            if (end != std::string::npos) {
                std::string inner = text.substr(i + 1, end - i - 1);
                if (inner.find("http") == 0 || inner.find("mailto:") == 0) {
                    r += "<a href=\"" + inner + "\">" + inner + "</a>";
                    i = end + 1;
                    continue;
                }
            }
        }

        switch (c) {
            case '&': r += "&amp;";  break;
            case '<': r += "&lt;";   break;
            case '>': r += "&gt;";   break;
            default:  r += c;
        }
        i++;
    }
    return r;
}

std::string RenderMarkdown(const std::string& md) {
    std::vector<std::string> lines;
    {
        std::istringstream ss(md);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
        }
    }

    auto getFenceInfo = [](const std::string& line) -> std::pair<bool, std::string> {
        if (line.size() < 3) return {false, ""};
        char fc = line[0];
        if (fc != '`' && fc != '~') return {false, ""};
        size_t cnt = 0;
        while (cnt < line.size() && line[cnt] == fc) cnt++;
        if (cnt < 3) return {false, ""};
        std::string lang = line.substr(cnt);
        while (!lang.empty() && (lang.front() == ' ' || lang.front() == '\t')) lang.erase(lang.begin());
        while (!lang.empty() && (lang.back() == ' ' || lang.back() == '\r')) lang.pop_back();
        return {true, lang};
    };

    auto isClosingFence = [](const std::string& line, char fenceChar) -> bool {
        if (line.size() < 3) return false;
        for (char c : line)
            if (c != fenceChar && c != ' ' && c != '\t') return false;
        size_t cnt = 0;
        for (char c : line) if (c == fenceChar) cnt++;
        return cnt >= 3;
    };

    std::string html;
    html.reserve(md.size() * 3);

    bool inCode    = false;
    bool inMermaid = false;
    char fenceChar = '`';
    std::string mermaidBuf;

    bool inTidbit = false;
    std::string tidbitSpeaker;
    std::string tidbitBuf;

    bool inConversation = false;
    std::string convTitle;
    std::string convBuf;

    int pendingChId = -1;

    bool inUL = false;
    bool inOL = false;
    bool inBQ = false;
    bool inTable = false;

    std::vector<std::string> paraLines;

    auto flushParagraph = [&]() {
        if (paraLines.empty()) return;
        std::string para;
        for (size_t k = 0; k < paraLines.size(); k++) {
            std::string ln = paraLines[k];
            bool hardBreak = ln.size() >= 2 && ln[ln.size()-1] == ' ' && ln[ln.size()-2] == ' ';
            if (hardBreak) ln.resize(ln.size() - 2);
            if (k > 0) para += hardBreak ? "<br>" : " ";
            para += ln;
        }
        html += "<p>" + ProcessInline(para) + "</p>\n";
        paraLines.clear();
    };

    auto closeLists = [&]() {
        if (inUL) { html += "</ul>\n"; inUL = false; }
        if (inOL) { html += "</ol>\n"; inOL = false; }
    };

    auto closeBlockquote = [&]() {
        if (inBQ) { html += "</blockquote>\n"; inBQ = false; }
    };

    auto closeTable = [&]() {
        if (inTable) { html += "</tbody></table>\n"; inTable = false; }
    };

    for (size_t i = 0; i < lines.size(); i++) {
        const std::string& raw = lines[i];

        // :::tidbit[Speaker] ... ::: block
        if (!inTidbit && raw.size() > 10 && raw.substr(0, 10) == ":::tidbit[") {
            size_t close = raw.find(']', 10);
            if (close != std::string::npos) {
                flushParagraph(); closeLists(); closeBlockquote(); closeTable();
                tidbitSpeaker = raw.substr(10, close - 10);
                tidbitBuf.clear();
                inTidbit = true;
                continue;
            }
        }
        if (inTidbit) {
            if (raw == ":::") {
                html += "<details class=\"tidbit\">\n"
                        "<summary>" + EscapeHTML(tidbitSpeaker) + "</summary>\n"
                        "<div class=\"tidbit-body\">" + RenderMarkdown(tidbitBuf) + "</div>\n"
                        "</details>\n";
                inTidbit = false;
                tidbitBuf.clear();
                tidbitSpeaker.clear();
            } else {
                tidbitBuf += raw + "\n";
            }
            continue;
        }

        // :::conversation[Title] ... ::: block
        if (!inConversation && raw.size() > 16 && raw.substr(0, 16) == ":::conversation[") {
            size_t close = raw.find(']', 16);
            if (close != std::string::npos) {
                flushParagraph(); closeLists(); closeBlockquote(); closeTable();
                convTitle = raw.substr(16, close - 16);
                convBuf.clear();
                inConversation = true;
                continue;
            }
        }
        if (inConversation) {
            if (raw == ":::") {
                // Parse Q:/A: pairs and render as collapsed details
                std::vector<std::pair<std::string,std::string>> qas;
                {
                    std::istringstream bss(convBuf);
                    std::string bline;
                    std::string curQ, curA;
                    bool inA = false;
                    while (std::getline(bss, bline)) {
                        if (!bline.empty() && bline.back() == '\r') bline.pop_back();
                        if (bline.rfind("Q: ", 0) == 0) {
                            if (!curQ.empty()) qas.push_back({curQ, curA});
                            curQ = bline.substr(3); curA = ""; inA = false;
                        } else if (bline.rfind("A: ", 0) == 0) {
                            curA = bline.substr(3); inA = true;
                        } else if (inA && !bline.empty()) {
                            curA += "\n" + bline;
                        }
                    }
                    if (!curQ.empty()) qas.push_back({curQ, curA});
                }
                std::string summary = "\xf0\x9f\x92\xac " + std::to_string(qas.size()) + " question";
                if (qas.size() != 1) summary += "s";
                summary += " \xe2\x80\x94 " + EscapeHTML(convTitle);
                html += "<details class=\"conversation\">\n"
                        "<summary>" + summary + "</summary>\n"
                        "<div class=\"conversation-body\">\n";
                for (auto& [q, a] : qas) {
                    html += "<div class=\"qa-turn\">"
                            "<div class=\"qa-q\">" + ProcessInline(q) + "</div>"
                            "<div class=\"qa-a\">" + ProcessInline(a) + "</div>"
                            "</div>\n";
                }
                html += "</div>\n</details>\n";
                inConversation = false;
                convBuf.clear();
                convTitle.clear();
            } else {
                convBuf += raw + "\n";
            }
            continue;
        }

        // Strip internal markers (<!-- ch:N -->, <!-- tb:N -->, <!-- qa:N -->)
        // and capture chapter ID for data-ch-id injection on the next <h2>.
        if (raw.rfind("<!-- ch:", 0) == 0) {
            size_t end = raw.find(" -->", 8);
            if (end != std::string::npos) {
                try { pendingChId = std::stoi(raw.substr(8, end - 8)); } catch (...) {}
            }
            flushParagraph();
            continue;
        }
        if (raw.rfind("<!-- tb:", 0) == 0 || raw.rfind("<!-- qa:", 0) == 0) {
            flushParagraph();
            continue;
        }

        if (inCode || inMermaid) {
            if (isClosingFence(raw, fenceChar)) {
                if (inMermaid) {
                    html += "<div class=\"mermaid-wrapper\">"
                            "<div class=\"mermaid\">\n" + mermaidBuf + "</div></div>\n";
                    mermaidBuf.clear();
                    inMermaid = false;
                } else {
                    html += "</code></pre>\n";
                    inCode = false;
                }
            } else {
                if (inMermaid)
                    mermaidBuf += raw + "\n";
                else
                    html += EscapeHTML(raw) + "\n";
            }
            continue;
        }

        auto [isFence, lang] = getFenceInfo(raw);
        if (isFence) {
            flushParagraph();
            closeLists();
            closeBlockquote();
            closeTable();
            fenceChar = raw[0];
            if (lang == "mermaid") {
                inMermaid = true;
            } else {
                inCode = true;
                if (!lang.empty())
                    html += "<pre><code class=\"language-" + lang + "\">";
                else
                    html += "<pre><code>";
            }
            continue;
        }

        if (raw.empty()) {
            flushParagraph();
            closeLists();
            closeBlockquote();
            closeTable();
            continue;
        }

        if (!raw.empty() && i + 1 < lines.size()) {
            const std::string& nxt = lines[i + 1];
            bool allEq = !nxt.empty() && nxt.find_first_not_of("= \t") == std::string::npos
                         && nxt.find('=') != std::string::npos;
            bool allDash = nxt.size() >= 3 && nxt.find_first_not_of("- \t") == std::string::npos
                           && nxt.find('-') != std::string::npos;
            if (allEq || allDash) {
                flushParagraph();
                closeLists();
                closeBlockquote();
                closeTable();
                int lvl = allEq ? 1 : 2;
                html += "<h" + std::to_string(lvl) + ">"
                      + ProcessInline(raw)
                      + "</h" + std::to_string(lvl) + ">\n";
                i++;
                continue;
            }
        }

        if (raw[0] == '#') {
            int lvl = 0;
            while (lvl < 6 && lvl < (int)raw.size() && raw[lvl] == '#') lvl++;
            if (lvl < (int)raw.size() && raw[lvl] == ' ') {
                flushParagraph();
                closeLists();
                closeBlockquote();
                closeTable();
                std::string heading = raw.substr(lvl + 1);
                {
                    size_t p = heading.find_last_not_of("# ");
                    if (p != std::string::npos) heading = heading.substr(0, p + 1);
                }
                std::string attrs;
                if (lvl == 2 && pendingChId >= 0) {
                    attrs = " data-ch-id=\"" + std::to_string(pendingChId) + "\"";
                    pendingChId = -1;
                }
                html += "<h" + std::to_string(lvl) + attrs + ">"
                      + ProcessInline(heading)
                      + "</h" + std::to_string(lvl) + ">\n";
                continue;
            }
        }

        {
            std::string stripped;
            for (char c : raw) if (c != ' ' && c != '\t') stripped += c;
            if (stripped.size() >= 3 && paraLines.empty() &&
                (stripped.find_first_not_of('-') == std::string::npos ||
                 stripped.find_first_not_of('*') == std::string::npos ||
                 stripped.find_first_not_of('_') == std::string::npos)) {
                closeLists();
                closeBlockquote();
                closeTable();
                html += "<hr>\n";
                continue;
            }
        }

        if (raw.size() >= 2 && raw[0] == '>' && (raw[1] == ' ' || raw[1] == '\t')) {
            flushParagraph();
            closeLists();
            closeTable();
            if (!inBQ) { html += "<blockquote>\n"; inBQ = true; }
            html += "<p>" + ProcessInline(raw.substr(2)) + "</p>\n";
            continue;
        }
        if (inBQ && !raw.empty()) {
            closeBlockquote();
        }

        if (raw.size() >= 2 && (raw[0] == '-' || raw[0] == '*' || raw[0] == '+') && raw[1] == ' ') {
            flushParagraph();
            closeBlockquote();
            closeTable();
            if (inOL) { html += "</ol>\n"; inOL = false; }
            if (!inUL) { html += "<ul>\n"; inUL = true; }
            html += "<li>" + ProcessInline(raw.substr(2)) + "</li>\n";
            continue;
        }

        {
            size_t j = 0;
            while (j < raw.size() && std::isdigit((unsigned char)raw[j])) j++;
            if (j > 0 && j < raw.size() - 1 && raw[j] == '.' && raw[j+1] == ' ') {
                flushParagraph();
                closeBlockquote();
                closeTable();
                if (inUL) { html += "</ul>\n"; inUL = false; }
                if (!inOL) { html += "<ol>\n"; inOL = true; }
                html += "<li>" + ProcessInline(raw.substr(j + 2)) + "</li>\n";
                continue;
            }
        }

        if (raw.find('|') != std::string::npos) {
            bool isTblHeader = false;
            if (!inTable && i + 1 < lines.size()) {
                const std::string& sep = lines[i + 1];
                std::string stripped;
                for (char c : sep) if (c != ' ' && c != '\t') stripped += c;
                if (!stripped.empty() && stripped.find_first_not_of("-|:") == std::string::npos
                    && stripped.find('|') != std::string::npos) {
                    isTblHeader = true;
                }
            }
            if (isTblHeader) {
                flushParagraph();
                closeLists();
                closeBlockquote();
                closeTable();
                auto splitCells = [](const std::string& row) -> std::vector<std::string> {
                    std::vector<std::string> cells;
                    std::istringstream ss(row);
                    std::string token;
                    while (std::getline(ss, token, '|')) {
                        while (!token.empty() && token.front() == ' ') token.erase(token.begin());
                        while (!token.empty() && token.back() == ' ') token.pop_back();
                        if (!token.empty()) cells.push_back(token);
                    }
                    return cells;
                };
                html += "<table>\n<thead><tr>\n";
                for (auto& cell : splitCells(raw))
                    html += "  <th>" + ProcessInline(cell) + "</th>\n";
                html += "</tr></thead>\n<tbody>\n";
                inTable = true;
                i++;
                continue;
            }
            if (inTable) {
                auto splitCells = [](const std::string& row) -> std::vector<std::string> {
                    std::vector<std::string> cells;
                    std::istringstream ss(row);
                    std::string token;
                    while (std::getline(ss, token, '|')) {
                        while (!token.empty() && token.front() == ' ') token.erase(token.begin());
                        while (!token.empty() && token.back() == ' ') token.pop_back();
                        if (!token.empty()) cells.push_back(token);
                    }
                    return cells;
                };
                html += "<tr>\n";
                for (auto& cell : splitCells(raw))
                    html += "  <td>" + ProcessInline(cell) + "</td>\n";
                html += "</tr>\n";
                continue;
            }
        }

        if (inUL || inOL) {
            closeLists();
        }

        paraLines.push_back(raw);
    }

    flushParagraph();
    closeLists();
    closeBlockquote();
    closeTable();
    if (inCode)    html += "</code></pre>\n";
    if (inMermaid) html += "<div class=\"mermaid-wrapper\"><div class=\"mermaid\">\n"
                           + mermaidBuf + "</div></div>\n";
    if (inTidbit)  html += "<details class=\"tidbit\">\n"
                           "<summary>" + EscapeHTML(tidbitSpeaker) + "</summary>\n"
                           "<div class=\"tidbit-body\">" + RenderMarkdown(tidbitBuf) + "</div>\n"
                           "</details>\n";

    return html;
}

std::string InjectNoteSpans(const std::string& renderedHtml,
                            const std::map<int,std::string>& noteTexts) {
    // ProcessInline escapes < and > so the comment arrives in the HTML body as
    // the literal text  &lt;!-- note:N --&gt;  — match that form.
    const std::string prefix = "&lt;!-- note:";
    const std::string suffix = " --&gt;";
    std::string result;
    result.reserve(renderedHtml.size());
    size_t pos = 0;
    while (pos < renderedHtml.size()) {
        size_t found = renderedHtml.find(prefix, pos);
        if (found == std::string::npos) {
            result += renderedHtml.substr(pos);
            break;
        }
        result += renderedHtml.substr(pos, found - pos);
        size_t numStart = found + prefix.size();
        size_t numEnd   = renderedHtml.find(suffix, numStart);
        if (numEnd == std::string::npos) {
            result += renderedHtml.substr(found);
            break;
        }
        std::string numStr = renderedHtml.substr(numStart, numEnd - numStart);
        int noteId = 0;
        try { noteId = std::stoi(numStr); } catch (...) {}
        std::string noteText;
        auto it = noteTexts.find(noteId);
        if (it != noteTexts.end()) noteText = it->second;
        result += "<span class=\"note-marker\" data-note-id=\""
               + std::to_string(noteId)
               + "\" data-note-text=\""
               + EscapeHTML(noteText)
               + "\">\xf0\x9f\x93\x9d</span>";
        pos = numEnd + suffix.size();
    }
    return result;
}

std::string GetLLMReadme() {
    return R"MD(# MDViewer — Syntax Reference for LLM Authors

This is the authoritative reference for markdown that MDViewer renders.
Include it in your context before generating documents for MDViewer.

---

## Standard syntax

### Headings

Use `#` through `######` for H1–H6, or setext underlines for H1/H2:

```
# H1        ## H2       ### H3
Title       Subtitle
=====       --------
```

### Emphasis

```
**bold**   __bold__   *italic*   _italic_   ***bold italic***   ~~strikethrough~~
```

### Inline code

```
`code snippet`
```

### Links and images

```
[link text](https://example.com)
![alt text](path/to/image.png)
<https://auto-linked-url.com>
```

### Blockquote

```
> Quoted text.
```

### Horizontal rule

```
---
```

### Lists

```
- unordered       1. ordered
* unordered       2. ordered
+ unordered
```

### Tables

```
| Column A | Column B |
|----------|----------|
| cell     | cell     |
```

### Fenced code blocks with syntax highlighting

Always choose the language tag that matches the content:

| Content | Tag to use |
|---|---|
| C++ source | `cpp` |
| Python source | `python` |
| JavaScript / TypeScript | `js` / `ts` |
| Rust | `rust` |
| Go | `go` |
| Shell / Bash | `bash` |
| x86 assembly (NASM) | `nasm` |
| SQL | `sql` |
| JSON | `json` |
| YAML | `yaml` |
| Any other language | its highlight.js identifier |
| ASCII art, diagrams, terminal output, prose — **no code** | `text` |

**Rule:** use `text` only when the block contains no code — ASCII art,
box-drawing diagrams, terminal session output, or plain prose excerpts.
The moment the block contains keywords, strings, or identifiers from a
programming language, switch to that language's tag so the reader gets
syntax highlighting and the correct Copy-button label.

```cpp
// correct — C++ gets highlighted
int main() { return 0; }
```

```text
┌─────────────────────┐
│  ASCII art box      │  ← correct use of text: no code inside
└─────────────────────┘
```

A **Copy** button appears on hover in the top-right corner of every fenced
code block. Clicking it copies the block's plain text to the Mac clipboard.

### Diagrams (Mermaid)

    ```mermaid
    graph LR
        A --> B --> C
    ```

Click the rendered diagram to zoom in.

---

## MDViewer extensions

### Tidbit — collapsible aside from a named voice

```
:::tidbit[Speaker Name]
Content goes here. Full markdown supported inside.
:::
```

Renders as a collapsed `<details>` widget. The reader clicks to reveal it.
Use tidbits for entertaining or supplementary commentary alongside chapters —
quotes, opinions, or colour from a named persona.

**Example:**

```
:::tidbit[Bjarne Stroustrup]
"I could have hidden vtables entirely. I chose not to.
The indirection is the point — **you** decide what's virtual."
:::
```

**Rules:**
- `Speaker Name` is displayed in the collapsed summary line.
- The body supports any MDViewer markdown: paragraphs, bold/italic, inline
  code, lists, blockquotes. Do not nest tidbits.
- Put a blank line before and after the block for clean paragraph spacing.

**Suggested voices for technical documents:**
- Language inventors: Bjarne Stroustrup (C++), James Gosling (Java),
  Guido van Rossum (Python), Yukihiro Matsumoto (Ruby)
- Systems pioneers: Dennis Ritchie, Ken Thompson, Linus Torvalds
- Industry figures: Bill Gates, Donald Knuth, Edsger Dijkstra
- Historical: Ada Lovelace, Alan Turing, Grace Hopper

---

## What MDViewer does NOT support

- Raw HTML passthrough (`<div>`, `<span>`, etc. are not rendered)
- Footnotes
- Definition lists
- Task checkboxes (`- [x]`)
- Nested tidbits
)MD";
}
