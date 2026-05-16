#include "creator.h"
#include "project.h"
#include <chrono>
#include <clocale>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static fs::path make_temp_dir() {
    auto ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto p = fs::temp_directory_path() / ("mdviewer_creator_" + std::to_string(ns));
    fs::create_directories(p);
    return p;
}

int test_creator() {
    int failures = 0;

    // BuildPrompt includes the topic, style, and character names.
    {
        GenerationRequest req;
        req.topic      = "black holes";
        req.style      = "children's book";
        req.characters = {"Albert Einstein", "Carl Sagan"};
        req.projectContext = "";

        std::string prompt = BuildPrompt(req, "");
        bool hasTopic  = prompt.find("black holes")    != std::string::npos;
        bool hasStyle  = prompt.find("children")       != std::string::npos;
        bool hasEin    = prompt.find("Albert Einstein") != std::string::npos;
        bool hasSagan  = prompt.find("Carl Sagan")      != std::string::npos;
        if (!hasTopic || !hasStyle || !hasEin || !hasSagan) {
            std::cerr << "FAIL [build-prompt-fields]: topic=" << hasTopic
                      << " style=" << hasStyle
                      << " einstein=" << hasEin
                      << " sagan=" << hasSagan << "\n";
            ++failures;
        } else {
            std::cout << "PASS [build-prompt-fields]\n";
        }
    }

    // BuildPrompt explicitly instructs the LLM to use the characters for tidbits.
    {
        GenerationRequest req;
        req.topic      = "black holes";
        req.style      = "children's book";
        req.characters = {"Albert Einstein", "Carl Sagan"};

        std::string prompt = BuildPrompt(req, "");
        bool hasEin       = prompt.find("Albert Einstein") != std::string::npos;
        bool hasTidbitInstr = prompt.find("tidbit") != std::string::npos
                           && prompt.find("Albert Einstein") != std::string::npos;
        if (!hasEin || !hasTidbitInstr) {
            std::cerr << "FAIL [build-prompt-tidbit-instruction]: "
                      << "characters should appear alongside tidbit instruction\n";
            ++failures;
        } else {
            std::cout << "PASS [build-prompt-tidbit-instruction]\n";
        }
    }

    // BuildPrompt includes a reminder to use the mdviewer/story-teller skill.
    {
        GenerationRequest req;
        req.topic      = "black holes";
        req.style      = "children's book";
        req.characters = {};

        std::string prompt = BuildPrompt(req, "");
        bool hasOutputFormat = prompt.find("Output format") != std::string::npos
                            || prompt.find("no prose") != std::string::npos
                            || prompt.find("no explanation") != std::string::npos;
        bool noSkillInvocation = prompt.find("use your") == std::string::npos
                              && prompt.find("mdviewer skill") == std::string::npos;
        if (!hasOutputFormat || !noSkillInvocation) {
            std::cerr << "FAIL [build-prompt-skill-reminder]: "
                      << "output format instruction missing or skill invocation present\n";
            ++failures;
        } else {
            std::cout << "PASS [build-prompt-skill-reminder]\n";
        }
    }

    // BuildPrompt embeds the tidbit syntax reference so the LLM knows the format.
    {
        GenerationRequest req;
        req.topic      = "volcanoes";
        req.style      = "horror";
        req.characters = {};
        req.projectContext = "";

        std::string prompt = BuildPrompt(req, "");
        bool hasTidbitSyntax = prompt.find(":::tidbit") != std::string::npos;
        if (!hasTidbitSyntax) {
            std::cerr << "FAIL [build-prompt-syntax]: :::tidbit not found in prompt\n";
            ++failures;
        } else {
            std::cout << "PASS [build-prompt-syntax]\n";
        }
    }

    // BuildPrompt prepends project context when provided.
    {
        GenerationRequest req;
        req.topic      = "the moon";
        req.style      = "noir";
        req.characters = {};
        req.projectContext = "This story takes place in 1920s Chicago.";

        std::string prompt = BuildPrompt(req, "");
        bool hasContext = prompt.find("1920s Chicago") != std::string::npos;
        if (!hasContext) {
            std::cerr << "FAIL [build-prompt-context]: project context not found in prompt\n";
            ++failures;
        } else {
            std::cout << "PASS [build-prompt-context]\n";
        }
    }

    // BuildPatchPrompt includes the original block, the instruction, and the syntax ref.
    {
        std::string original = ":::tidbit[Sherlock Holmes]\nElementary.\n:::";
        std::string instruction = "make this more dramatic";

        std::string prompt = BuildPatchPrompt(original, instruction, "");
        bool hasOriginal    = prompt.find("Elementary")          != std::string::npos;
        bool hasInstruction = prompt.find("more dramatic")       != std::string::npos;
        bool hasSyntax      = prompt.find(":::tidbit")           != std::string::npos;
        if (!hasOriginal || !hasInstruction || !hasSyntax) {
            std::cerr << "FAIL [build-patch-prompt]: original=" << hasOriginal
                      << " instruction=" << hasInstruction
                      << " syntax=" << hasSyntax << "\n";
            ++failures;
        } else {
            std::cout << "PASS [build-patch-prompt]\n";
        }
    }

    // BuildTranslationPrompt preserves structure and names the target language.
    {
        std::string prompt = BuildTranslationPrompt("<!-- ch:0 -->\n## Chapter 1: Hello\n",
                                                    "Spanish",
                                                    "",
                                                    "Add a pinyin line below each Chinese sentence.");
        bool hasLang = prompt.find("Spanish") != std::string::npos;
        bool hasMarker = prompt.find("ch:N") != std::string::npos
                      || prompt.find("<!-- ch:0 -->") != std::string::npos;
        bool hasSource = prompt.find("Chapter 1") != std::string::npos;
        bool hasAdapt = prompt.find("culturally relevant") != std::string::npos
                     && prompt.find("instead of literal") != std::string::npos;
        bool hasNewFile = prompt.find("translated markdown document") != std::string::npos;
        bool hasExtra = prompt.find("pinyin line") != std::string::npos;
        if (!hasLang || !hasMarker || !hasSource || !hasAdapt || !hasNewFile || !hasExtra) {
            std::cerr << "FAIL [build-translation-prompt]\n";
            ++failures;
        } else {
            std::cout << "PASS [build-translation-prompt]\n";
        }
    }

    // CleanMarkdownResponse removes common LLM translation wrappers.
    {
        std::string wrapped =
            "```markdown\n"
            "技能文件无法访问，但请求中已包含完整的语法参考。\n\n"
            "---\n\n"
            "# 第一支疫苗的故事\n\n"
            "<!-- ch:0 -->\n"
            "## 第一章：可怕的天花\n\n"
            "正文。\n"
            "```";
        std::string cleaned = CleanMarkdownResponse(wrapped);
        bool noFence = cleaned.find("```markdown") == std::string::npos;
        bool noPreface = cleaned.find("技能文件") == std::string::npos;
        bool startsDoc = cleaned.rfind("# 第一支疫苗", 0) == 0;
        if (!noFence || !noPreface || !startsDoc) {
            std::cerr << "FAIL [clean-markdown-response]: cleaned='" << cleaned << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [clean-markdown-response]\n";
        }
    }

    {
        std::string jsonWrapped =
            "{\n"
            "  \"markdown\": \"# 标题\\n\\n<!-- ch:0 -->\\n## 第一章：开始\\n\\n正文。\"\n"
            "}";
        std::string cleaned = CleanMarkdownResponse(jsonWrapped);
        bool startsDoc = cleaned.rfind("# 标题", 0) == 0;
        bool noJson = cleaned.find("\"markdown\"") == std::string::npos;
        bool hasMarker = cleaned.find("<!-- ch:0 -->") != std::string::npos;
        if (!startsDoc || !noJson || !hasMarker) {
            std::cerr << "FAIL [clean-json-markdown-response]: cleaned='" << cleaned << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [clean-json-markdown-response]\n";
        }
    }

    {
        std::string response =
            "The translation is below:\n\n"
            "<!-- ch:0 -->\n"
            "## 第一章：开始\n\n"
            "正文。\n";
        std::string cleaned = CleanMarkdownResponse(response);
        bool startsMarker = cleaned.rfind("<!-- ch:0 -->", 0) == 0;
        bool noPreface = cleaned.find("translation is below") == std::string::npos;
        if (!startsMarker || !noPreface) {
            std::cerr << "FAIL [clean-preface-to-marker]: cleaned='" << cleaned << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [clean-preface-to-marker]\n";
        }
    }

    // LLM preamble + ```markdown fence wrapper + trailing ``` + post-explanation prose.
    // This is the real-world pattern: the LLM says "Here is the document:" then wraps
    // the content in ```markdown...``` and adds a summary paragraph after.
    {
        std::string response =
            "I have the syntax reference. Let me generate the document now.\n\n"
            "```markdown\n"
            "# Title\n\n"
            "## Chapter 1: Beginning\n\n"
            "Body text here.\n"
            "```\n\n"
            "Here is the full document covering the topic.\n";
        std::string cleaned = CleanMarkdownResponse(response);
        bool startsH1    = cleaned.rfind("# Title", 0) == 0;
        bool noPreamble  = cleaned.find("syntax reference") == std::string::npos;
        bool noFence     = cleaned.find("```markdown") == std::string::npos;
        bool noPostAmble = cleaned.find("full document covering") == std::string::npos;
        bool noBareFence = cleaned.find("\n```") == std::string::npos;
        if (!startsH1 || !noPreamble || !noFence || !noPostAmble || !noBareFence) {
            std::cerr << "FAIL [clean-preamble-fence-postamble]: cleaned='"
                      << cleaned << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [clean-preamble-fence-postamble]\n";
        }
    }

    {
        std::string response =
            "# 标题\n\n"
            "<!-- tb:8 -->\n"
            "::::tidbit[李白]\n"
            "诗句也能讲科学。\n"
            "::::\n";
        std::string cleaned = CleanMarkdownResponse(response);
        bool hasOpen = cleaned.find(":::tidbit[李白]") != std::string::npos;
        bool hasClose = cleaned.find("\n:::\n") != std::string::npos;
        bool noLongFence = cleaned.find("::::") == std::string::npos;
        if (!hasOpen || !hasClose || !noLongFence) {
            std::cerr << "FAIL [clean-tidbit-fences]: cleaned='" << cleaned << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [clean-tidbit-fences]\n";
        }
    }

    // BuildPrompt instructs the LLM to divide the story into numbered chapters.
    {
        GenerationRequest req;
        req.topic = "black holes";
        req.style = "children's book";

        std::string prompt = BuildPrompt(req, "");
        bool hasChapter   = prompt.find("## Chapter") != std::string::npos
                         || prompt.find("Chapter N")  != std::string::npos;
        bool hasChId      = prompt.find("ch:") != std::string::npos
                         || prompt.find("<!-- ch") != std::string::npos
                         || prompt.find("chapter id") != std::string::npos
                         || prompt.find("numbered") != std::string::npos;
        if (!hasChapter || !hasChId) {
            std::cerr << "FAIL [build-prompt-chapter-structure]: "
                      << "chapter=" << hasChapter << " id=" << hasChId << "\n";
            ++failures;
        } else {
            std::cout << "PASS [build-prompt-chapter-structure]\n";
        }
    }

    // StampChapters injects <!-- ch:N --> markers before ## Chapter headings.
    {
        std::string content =
            "# My Story\n\n"
            "## Chapter 1: The Start\n\nSome text.\n\n"
            "## Chapter 2: The End\n\nMore text.\n";

        auto [stamped, count] = StampChapters(content, 0);
        bool hasCh0   = stamped.find("<!-- ch:0 -->") != std::string::npos;
        bool hasCh1   = stamped.find("<!-- ch:1 -->") != std::string::npos;
        bool countOk  = count == 2;
        bool orderOk  = stamped.find("<!-- ch:0 -->") < stamped.find("## Chapter 1");
        if (!hasCh0 || !hasCh1 || !countOk || !orderOk) {
            std::cerr << "FAIL [stamp-chapters]: ch0=" << hasCh0
                      << " ch1=" << hasCh1 << " count=" << count
                      << " order=" << orderOk << "\n";
            ++failures;
        } else {
            std::cout << "PASS [stamp-chapters]\n";
        }
    }

    // StampChapters also supports localized level-2 chapter headings.
    {
        std::string content =
            "# 我的故事\n\n"
            "## 一章：开始\n\n正文。\n\n"
            "## 二章：结束\n\n更多正文。\n";

        auto [stamped, count] = StampChapters(content, 4);
        bool hasCh4 = stamped.find("<!-- ch:4 -->\n## 一章") != std::string::npos;
        bool hasCh5 = stamped.find("<!-- ch:5 -->\n## 二章") != std::string::npos;
        if (!hasCh4 || !hasCh5 || count != 2) {
            std::cerr << "FAIL [stamp-localized-chapters]: ch4=" << hasCh4
                      << " ch5=" << hasCh5 << " count=" << count << "\n";
            ++failures;
        } else {
            std::cout << "PASS [stamp-localized-chapters]\n";
        }
    }

    // ValidateGeneratedStory rejects incomplete generations before saving.
    {
        auto bad = ValidateGeneratedStory("# Title\n\nToo short.\n");
        std::string goodContent =
            "# Story\n\n"
            "## Chapter 1: Start\n\n"
            "This is a reasonably long generated chapter with enough text to look like a real story. "
            "It keeps going for a while so the length guard does not mistake it for a tiny fragment. "
            "The characters learn something useful and the section has enough material to be edited later by the app. "
            "A fourth sentence adds more detail about the setting and the people in it. "
            "A fifth sentence makes the chapter substantial enough for saving.\n\n"
            ":::tidbit[Charles Darwin]\n"
            "A complete observation deserves a complete note.\n"
            ":::\n\n"
            "## Chapter 2: More\n\n"
            "Another paragraph continues the narrative with more details. "
            "The characters compare notes and decide what to do next. "
            "Their plan becomes clearer with each observation. "
            "The story gives the reader enough context to follow along. "
            "The chapter now has five complete sentences.\n\n"
            ":::tidbit[Sherlock Holmes]\n"
            "The structure is the clue.\n"
            ":::\n";
        auto good = ValidateGeneratedStory(goodContent);
        std::string localizedContent =
            "# 故事\n\n"
            "## 一章：开始\n\n"
            "这是一段足够长的中文故事内容。"
            "它用来确认验证器接受本地化的二级章节标题。"
            "故事继续发展，人物一起学习并发现新的知识。"
            "文本长度足够通过片段检查。"
            "这里还有更多叙述，让章节看起来完整。\n\n"
            ":::tidbit[李白]\n"
            "诗意也可以帮助孩子理解科学。\n"
            ":::\n\n"
            "## 二章：发现\n\n"
            "第二章继续讲述他们如何观察。"
            "他们认真讨论每一个问题。"
            "他们也进行简单的实验。"
            "复杂的概念被变成容易理解的故事。"
            "这一段确保内容不是很短的残缺回答。\n\n"
            ":::tidbit[李小龙]\n"
            "保持好奇，也保持行动。\n"
            ":::\n";
        auto localized = ValidateGeneratedStory(localizedContent);
        if (bad.ok || !good.ok || !localized.ok) {
            std::cerr << "FAIL [validate-generated-story]: bad=" << bad.ok
                      << " good=" << good.ok << " localized=" << localized.ok
                      << " error='" << good.error << localized.error << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [validate-generated-story]\n";
        }
    }

    // BuildRepairPrompt carries the original request and the validation failure.
    {
        std::string prompt = BuildRepairPrompt("Write about vaccines.",
                                               "chapter 3 had fewer than five sentences");
        bool hasOriginal = prompt.find("Write about vaccines") != std::string::npos;
        bool hasError = prompt.find("chapter 3") != std::string::npos;
        bool hasReplacement = prompt.find("complete replacement") != std::string::npos;
        if (!hasOriginal || !hasError || !hasReplacement) {
            std::cerr << "FAIL [build-repair-prompt]\n";
            ++failures;
        } else {
            std::cout << "PASS [build-repair-prompt]\n";
        }
    }

    // ChapterFilename produces a slug from the topic with a chapter number prefix.
    {
        std::string name = ChapterFilename("Black Holes & Neutron Stars", 3);
        bool hasPrefix  = name.rfind("ch03_", 0) == 0;
        bool hasMd      = name.size() > 3 && name.substr(name.size() - 3) == ".md";
        bool noSpaces   = name.find(' ') == std::string::npos;
        bool noAmpersand = name.find('&') == std::string::npos;
        if (!hasPrefix || !hasMd || !noSpaces || !noAmpersand) {
            std::cerr << "FAIL [chapter-filename]: got '" << name << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [chapter-filename]\n";
        }
    }

    // slugify must strip non-ASCII bytes even when the system locale (en_US.UTF-8)
    // makes isalnum(0xe2) return true. A raw 0xE2 byte in a filename causes
    // ofstream to fail on macOS HFS+ which requires valid UTF-8 paths.
    {
        // Temporarily switch to the system locale, matching wxWidgets app behaviour.
        const char* savedLocale = setlocale(LC_ALL, nullptr);
        setlocale(LC_ALL, "");

        // U+201C LEFT DOUBLE QUOTATION MARK = UTF-8: e2 80 9c
        const char curly[] = { static_cast<char>(0xe2), static_cast<char>(0x80),
                                static_cast<char>(0x9c), '\0' };
        std::string topic = std::string("learn about ") + curly + "blood letting";
        std::string name = ChapterFilename(topic, 1);

        setlocale(LC_ALL, savedLocale); // restore before any assertion

        bool allAscii = true;
        for (unsigned char c : name)
            if (c > 127) { allAscii = false; break; }
        if (!allAscii) {
            std::cerr << "FAIL [chapter-filename-utf8]: filename contains non-ASCII byte: '"
                      << name << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [chapter-filename-utf8]\n";
        }
    }

    // SaveChapter writes the content to disk and returns the path.
    {
        auto base = make_temp_dir();
        CreateProject(base.string(), "save-test");
        std::string proj = (base / "save-test").string();

        std::string content = "# Hello\n\nWorld\n";
        std::string path = SaveChapter(proj, "ch01_hello.md", content);
        bool fileExists = fs::exists(path);
        bool contentOk  = false;
        if (fileExists) {
            std::ifstream f(path);
            std::string got((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
            contentOk = (got == content);
        }
        if (!fileExists || !contentOk) {
            std::cerr << "FAIL [save-chapter]: exists=" << fileExists
                      << " content=" << contentOk << "\n";
            ++failures;
        } else {
            std::cout << "PASS [save-chapter]\n";
        }
        fs::remove_all(base);
    }

    return failures;
}
