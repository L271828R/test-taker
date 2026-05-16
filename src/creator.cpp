#include "creator.h"
#include "markdown.h"
#include "llm_response.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

static bool starts_with_h2(const std::string& line) {
    return line.rfind("## ", 0) == 0 && line.rfind("### ", 0) != 0;
}

static bool has_h2_heading(const std::string& content) {
    if (starts_with_h2(content)) return true;
    return content.find("\n## ") != std::string::npos;
}

static int sentence_count(const std::string& text) {
    int count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '.' || c == '!' || c == '?' || c == ';') ++count;
        if (text.compare(i, 3, "。") == 0 ||
            text.compare(i, 3, "！") == 0 ||
            text.compare(i, 3, "？") == 0) {
            ++count;
            i += 2;
        }
    }
    return count;
}

static std::vector<std::string> chapter_blocks(const std::string& content) {
    std::vector<std::string> blocks;
    std::istringstream in(content);
    std::string line;
    std::string current;
    bool inChapter = false;
    while (std::getline(in, line)) {
        if (starts_with_h2(line)) {
            if (inChapter && !current.empty()) blocks.push_back(current);
            current = line + "\n";
            inChapter = true;
        } else if (inChapter) {
            current += line + "\n";
        }
    }
    if (inChapter && !current.empty()) blocks.push_back(current);
    return blocks;
}

static std::string slugify(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        bool digit = (c >= '0' && c <= '9');
        if (alpha || digit)
            out += static_cast<char>(alpha && c < 'a' ? c + 32 : c);
        else if (!out.empty() && out.back() != '_')
            out += '_';
        if (out.size() >= 60) break;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

static std::string trim_copy(const std::string& s) {
    const std::string ws = " \t\r\n";
    auto start = s.find_first_not_of(ws);
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

static std::size_t first_document_line(const std::string& s) {
    std::istringstream in(s);
    std::string line;
    std::size_t offset = 0;
    while (std::getline(in, line)) {
        if (line.rfind("# ", 0) == 0 ||
            line.rfind("## ", 0) == 0 ||
            line.rfind("<!-- ch:", 0) == 0) {
            return offset;
        }
        offset += line.size() + 1;
    }
    return std::string::npos;
}

// Strip the last lone ``` (or ~~~) line plus any trailing non-heading prose.
// Called only when we know the response was fence-wrapped by the LLM.
static std::string strip_trailing_fence(const std::string& s) {
    std::istringstream in(s);
    std::string line;
    std::size_t offset = 0;
    std::size_t lastFenceOff = std::string::npos;
    std::size_t lastFenceLen = 0;

    while (std::getline(in, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "```" || line == "~~~") {
            lastFenceOff = offset;
            lastFenceLen = line.size();
        }
        offset += line.size() + 1;
    }

    if (lastFenceOff == std::string::npos) return s;

    std::size_t afterFence = lastFenceOff + lastFenceLen + 1; // +1 for \n
    std::string remainder = (afterFence < s.size()) ? trim_copy(s.substr(afterFence)) : "";

    if (!remainder.empty()) {
        bool hasHeading = remainder.rfind("# ",  0) == 0 || remainder.find("\n# ")  != std::string::npos
                       || remainder.rfind("## ", 0) == 0 || remainder.find("\n## ") != std::string::npos;
        if (hasHeading) return s;
    }

    return trim_copy(s.substr(0, lastFenceOff));
}

static std::string normalize_tidbit_fences(const std::string& s) {
    std::istringstream in(s);
    std::ostringstream out;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (!first) out << "\n";
        first = false;

        std::size_t start = line.find_first_not_of(" \t");
        std::string indent = start == std::string::npos ? "" : line.substr(0, start);
        std::string trimmed = start == std::string::npos ? "" : line.substr(start);
        while (!trimmed.empty() && trimmed.back() == '\r') trimmed.pop_back();

        std::size_t colons = 0;
        while (colons < trimmed.size() && trimmed[colons] == ':') ++colons;
        if (colons >= 3 && trimmed.compare(colons, 6, "tidbit") == 0) {
            out << indent << ":::" << trimmed.substr(colons);
        } else if (colons >= 3 && trimmed.find_first_not_of(':') == std::string::npos) {
            out << indent << ":::";
        } else {
            out << line;
        }
    }
    if (!s.empty() && s.back() == '\n') out << "\n";
    return out.str();
}

std::string CleanMarkdownResponse(const std::string& response) {
    std::string text = trim_copy(response);

    if (!text.empty() && text.front() == '{') {
        std::string markdown = ExtractJSONString(text, "markdown");
        if (markdown.empty()) markdown = ExtractJSONString(text, "content");
        if (!markdown.empty()) text = trim_copy(markdown);
    }

    if (text.rfind("```", 0) == 0) {
        auto firstLineEnd = text.find('\n');
        if (firstLineEnd != std::string::npos) {
            std::string fence = text.substr(0, firstLineEnd);
            if (fence == "```" || fence == "```markdown" || fence == "```md") {
                text = strip_trailing_fence(trim_copy(text.substr(firstLineEnd + 1)));
            }
        }
    }

    auto start = first_document_line(text);
    if (start != std::string::npos && start > 0) {
        std::string prefix = text.substr(0, start);
        bool prefixHasFence = prefix.find("```") != std::string::npos
                           || prefix.find("~~~") != std::string::npos;
        text = text.substr(start);
        if (prefixHasFence)
            text = strip_trailing_fence(text);
    }

    text = normalize_tidbit_fences(text);
    return trim_copy(text) + "\n";
}

std::string BuildPrompt(const GenerationRequest& req, const std::string& llmReadme) {
    std::ostringstream out;

    if (!req.projectContext.empty())
        out << "## Project context\n\n" << req.projectContext << "\n\n";

    out << "## Request\n\n"
        << req.topic << "\n\n";

    if (!req.style.empty())
        out << "Style: **" << req.style << "**\n\n";

    if (!req.characters.empty()) {
        out << "## Tidbit characters\n\n"
            << "Use the following characters for the `:::tidbit` sections. "
               "Each character must appear at least once; give them a distinct voice:\n\n";
        for (auto& ch : req.characters)
            out << "- " << ch << "\n";
        out << "\n";
    }

        out << "## Story structure\n\n"
        << "Divide the story into numbered chapters. Each chapter **must** start with a "
           "level-2 heading in exactly this format:\n\n"
           "```\n## Chapter N: Title\n```\n\n"
           "where N is a sequential integer starting at 1. "
           "Keep chapters in order with no gaps. "
           "Each chapter must contain at least five complete sentences of story text, "
           "not counting tidbit content. "
           "Every chapter must contain at least one `:::tidbit` block.\n\n";

    out << "## Output format\n\n"
        << "Return ONLY the raw MDViewer markdown document — no explanation, preamble, "
           "or commentary before or after it. Do not wrap the output in a markdown code "
           "fence (no ```markdown wrapper). "
           "Every tidbit must have content; do not emit empty tidbits.\n\n";

    std::string ref = llmReadme.empty() ? GetLLMReadme() : llmReadme;
    out << "## MDViewer syntax reference\n\n" << ref;

    return out.str();
}

ValidationResult ValidateGeneratedStory(const std::string& content) {
    if (content.size() < 500) {
        return {false, "LLM output was too short to be a complete story."};
    }
    if (!has_h2_heading(content)) {
        return {false, "LLM output did not contain any level-2 chapter headings."};
    }
    if (content.find(":::tidbit[") == std::string::npos) {
        return {false, "LLM output did not contain any :::tidbit[...] blocks."};
    }
    auto chapters = chapter_blocks(content);
    for (std::size_t i = 0; i < chapters.size(); ++i) {
        if (sentence_count(chapters[i]) < 5) {
            return {false, "LLM output chapter " + std::to_string(i + 1)
                           + " had fewer than five sentences."};
        }
    }
    return {true, ""};
}

std::string BuildRepairPrompt(const std::string& originalPrompt,
                              const std::string& validationError) {
    std::ostringstream out;
    out << originalPrompt
        << "\n\n## Previous output rejected\n\n"
        << validationError << "\n\n"
        << "Generate a complete replacement from scratch. Do not summarize. "
           "Do not return a partial fix. Ensure every chapter has at least five "
           "complete sentences of story text, includes a tidbit block, and follows "
           "the requested language.\n";
    return out.str();
}

std::string BuildPatchPrompt(const std::string& originalBlock,
                             const std::string& instruction,
                             const std::string& llmReadme,
                             const std::string& chapterContext) {
    std::ostringstream out;

    if (!chapterContext.empty()) {
        out << "## Story context\n\n"
            << "The following is the full chapter that contains the block you are rewriting. "
               "Use it to understand tone, characters, and what has already been said:\n\n"
            << "```\n" << chapterContext << "\n```\n\n";
    }

    out << "## Rewrite instruction\n\n"
        << instruction << "\n\n"
        << "## Original block\n\n"
        << "```\n" << originalBlock << "\n```\n\n"
        << "## Output format\n\n"
        << "Return ONLY the rewritten block — no prose before or after, "
           "no markdown code fence wrapper.\n\n";

    std::string ref = llmReadme.empty() ? GetLLMReadme() : llmReadme;
    out << "## MDViewer syntax reference\n\n" << ref;

    return out.str();
}

std::string BuildTranslationPrompt(const std::string& sourceMarkdown,
                                   const std::string& targetLanguage,
                                   const std::string& llmReadme,
                                   const std::string& extraInstruction) {
    std::ostringstream out;
    out << "## Translation request\n\n"
        << "Translate the following complete markdown document into "
        << targetLanguage << ".\n\n"
        << "Preserve the markdown structure, heading levels, fenced code blocks, image/link URLs, "
           "and HTML comment markers such as `<!-- ch:N -->` and `<!-- tb:N -->`. "
           "Translate visible prose and chapter titles. For `:::tidbit[...]` blocks, "
           "adapt the joke, aside, idiom, voice, and cultural reference so it feels natural, "
           "authentic, and culturally relevant in the target language instead of literal. "
           "Keep the tidbit block syntax valid and keep the same speaker name unless the "
           "source name has a standard target-language rendering. "
           "Return only the translated markdown document with no prose before or after.\n\n"
        << "## Additional translation instructions\n\n";

    if (trim_copy(extraInstruction).empty()) {
        out << "None.\n\n";
    } else {
        out << extraInstruction << "\n\n";
    }

    out << "## Source markdown\n\n"
        << "```markdown\n" << sourceMarkdown << "\n```\n\n";

    std::string ref = llmReadme.empty() ? GetLLMReadme() : llmReadme;
    out << "## MDViewer syntax reference\n\n" << ref;
    return out.str();
}

StampResult StampChapters(const std::string& content, int baseId) {
    std::string stamped;
    int count = 0;

    std::istringstream in(content);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (!first) stamped += "\n";
        first = false;
        if (starts_with_h2(line)) {
            stamped += "<!-- ch:" + std::to_string(baseId + count++) + " -->\n";
        }
        stamped += line;
    }
    if (!content.empty() && content.back() == '\n') stamped += "\n";
    return {stamped, count};
}

std::string ChapterFilename(const std::string& topic, int chapterNumber) {
    std::ostringstream prefix;
    prefix << "ch" << std::setw(2) << std::setfill('0') << chapterNumber << "_";
    return prefix.str() + slugify(topic) + ".md";
}

std::string SaveChapter(const std::string& projectDir,
                        const std::string& filename,
                        const std::string& content) {
    fs::path path = fs::path(projectDir) / filename;
    std::ofstream f(path);
    f << content;
    if (!f.good()) return "";
    return path.string();
}
