#include "editor.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static fs::path make_temp_dir() {
    auto ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto p = fs::temp_directory_path() / ("mdviewer_editor_" + std::to_string(ns));
    fs::create_directories(p);
    return p;
}

static const std::string kSampleChapter =
    "# Black Holes\n\n"
    "Some intro text.\n\n"
    "<!-- tb:3 -->\n"
    ":::tidbit[Albert Einstein]\n"
    "E=mc²\n"
    ":::\n\n"
    "Middle paragraph.\n\n"
    "<!-- tb:5 -->\n"
    ":::tidbit[Carl Sagan]\n"
    "We are star stuff.\n"
    ":::\n\n"
    "Final paragraph.\n";

int test_editor() {
    int failures = 0;

    // ExtractTidbit returns the :::tidbit...:::  block for a given ID.
    {
        std::string block = ExtractTidbit(kSampleChapter, 3);
        bool hasOpen    = block.find(":::tidbit[Albert Einstein]") != std::string::npos;
        bool hasContent = block.find("E=mc²")                     != std::string::npos;
        bool hasClose   = block.find(":::") != std::string::npos;
        bool noSagan    = block.find("Carl Sagan") == std::string::npos;
        if (!hasOpen || !hasContent || !hasClose || !noSagan) {
            std::cerr << "FAIL [extract-tidbit]: open=" << hasOpen
                      << " content=" << hasContent
                      << " close=" << hasClose
                      << " noSagan=" << noSagan
                      << "\n  got: '" << block << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [extract-tidbit]\n";
        }
    }

    // ExtractTidbit returns "" for an unknown ID.
    {
        std::string block = ExtractTidbit(kSampleChapter, 99);
        if (!block.empty()) {
            std::cerr << "FAIL [extract-tidbit-missing]: expected empty, got '" << block << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [extract-tidbit-missing]\n";
        }
    }

    // ExtractTidbit tolerates translated LLM output with indented or overlong fences.
    {
        std::string content =
            "# 中文故事\n\n"
            "<!-- tb:10 -->\n"
            "  ::::tidbit[李白]\n"
            "诗也可以解释科学。\n"
            "  ::::\n";
        std::string block = ExtractTidbit(content, 10);
        bool hasOpen = block.find("::::tidbit[李白]") != std::string::npos;
        bool hasText = block.find("诗也可以解释科学") != std::string::npos;
        if (!hasOpen || !hasText) {
            std::cerr << "FAIL [extract-tidbit-loose-fences]: got '" << block << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [extract-tidbit-loose-fences]\n";
        }
    }

    // PatchTidbit replaces the target block and preserves surrounding content.
    {
        std::string replacement =
            ":::tidbit[Albert Einstein]\n"
            "Energy and mass are equivalent — a profound truth!\n"
            ":::";
        std::string patched = PatchTidbit(kSampleChapter, 3, replacement);

        bool hasNew     = patched.find("profound truth")         != std::string::npos;
        bool hasOldGone = patched.find("E=mc²")                  == std::string::npos;
        bool hasSagan   = patched.find("We are star stuff")      != std::string::npos;
        bool hasIntro   = patched.find("Some intro text")        != std::string::npos;
        bool hasFinal   = patched.find("Final paragraph")        != std::string::npos;
        if (!hasNew || !hasOldGone || !hasSagan || !hasIntro || !hasFinal) {
            std::cerr << "FAIL [patch-tidbit]: new=" << hasNew
                      << " oldGone=" << hasOldGone
                      << " sagan=" << hasSagan
                      << " intro=" << hasIntro
                      << " final=" << hasFinal << "\n";
            ++failures;
        } else {
            std::cout << "PASS [patch-tidbit]\n";
        }
    }

    // PatchTidbit preserves the <!-- tb:N --> marker in the output.
    {
        std::string replacement = ":::tidbit[Albert Einstein]\nNew content.\n:::";
        std::string patched = PatchTidbit(kSampleChapter, 3, replacement);
        bool hasMarker = patched.find("<!-- tb:3 -->") != std::string::npos;
        if (!hasMarker) {
            std::cerr << "FAIL [patch-preserves-marker]: <!-- tb:3 --> not found after patch\n";
            ++failures;
        } else {
            std::cout << "PASS [patch-preserves-marker]\n";
        }
    }

    // ApplyTidbitPatch reads a file, patches it, and writes it back.
    {
        auto tmp = make_temp_dir();
        fs::path filepath = tmp / "ch.md";
        { std::ofstream f(filepath); f << kSampleChapter; }

        std::string replacement = ":::tidbit[Albert Einstein]\nUpdated on disk.\n:::";
        bool ok = ApplyTidbitPatch(filepath.string(), 3, replacement);

        std::string onDisk;
        { std::ifstream f(filepath);
          onDisk.assign(std::istreambuf_iterator<char>(f), {}); }

        bool updated = onDisk.find("Updated on disk") != std::string::npos;
        bool sagan   = onDisk.find("We are star stuff") != std::string::npos;
        if (!ok || !updated || !sagan) {
            std::cerr << "FAIL [apply-patch]: ok=" << ok
                      << " updated=" << updated
                      << " sagan=" << sagan << "\n";
            ++failures;
        } else {
            std::cout << "PASS [apply-patch]\n";
        }
        fs::remove_all(tmp);
    }

    // ExtractChapter returns the block between <!-- ch:N --> and the next marker (or EOF).
    {
        std::string content =
            "# Story\n\n"
            "<!-- ch:0 -->\n## Chapter 1: Start\n\nText one.\n\n"
            "<!-- ch:1 -->\n## Chapter 2: End\n\nText two.\n";

        std::string ch0 = ExtractChapter(content, 0);
        std::string ch1 = ExtractChapter(content, 1);
        std::string missing = ExtractChapter(content, 99);

        bool ch0HasText  = ch0.find("Text one")    != std::string::npos;
        bool ch0NoTwo    = ch0.find("Text two")    == std::string::npos;
        bool ch1HasText  = ch1.find("Text two")    != std::string::npos;
        bool missingEmpty = missing.empty();

        if (!ch0HasText || !ch0NoTwo || !ch1HasText || !missingEmpty) {
            std::cerr << "FAIL [extract-chapter]: ch0=" << ch0HasText
                      << " ch0NoTwo=" << ch0NoTwo
                      << " ch1=" << ch1HasText
                      << " missing=" << missingEmpty << "\n";
            ++failures;
        } else {
            std::cout << "PASS [extract-chapter]\n";
        }
    }

    // ApplyChapterPatch replaces a chapter section in a file, preserving the rest.
    {
        auto tmp = make_temp_dir();
        fs::path filepath = tmp / "story.md";
        {
            std::ofstream f(filepath);
            f << "# Story\n\n"
                 "<!-- ch:0 -->\n## Chapter 1: Start\n\nOld text.\n\n"
                 "<!-- ch:1 -->\n## Chapter 2: End\n\nKeep this.\n";
        }

        bool ok = ApplyChapterPatch(filepath.string(), 0,
                                    "<!-- ch:0 -->\n## Chapter 1: Start\n\nNew text.\n\n");
        std::string onDisk;
        { std::ifstream f(filepath);
          onDisk.assign(std::istreambuf_iterator<char>(f), {}); }

        bool hasNew  = onDisk.find("New text")  != std::string::npos;
        bool hasKeep = onDisk.find("Keep this") != std::string::npos;
        bool noOld   = onDisk.find("Old text")  == std::string::npos;

        if (!ok || !hasNew || !hasKeep || !noOld) {
            std::cerr << "FAIL [apply-chapter-patch]: ok=" << ok
                      << " hasNew=" << hasNew
                      << " hasKeep=" << hasKeep
                      << " noOld=" << noOld << "\n";
            ++failures;
        } else {
            std::cout << "PASS [apply-chapter-patch]\n";
        }
        fs::remove_all(tmp);
    }

    // ApplyChapterPatch re-adds the marker if the LLM stripped it.
    {
        auto tmp = make_temp_dir();
        fs::path filepath = tmp / "story.md";
        {
            std::ofstream f(filepath);
            f << "# Story\n\n"
                 "<!-- ch:0 -->\n## Chapter 1: Start\n\nOld text.\n\n"
                 "<!-- ch:1 -->\n## Chapter 2: End\n\nKeep this.\n";
        }

        // Pass newBlock WITHOUT the marker — simulates LLM stripping it.
        bool ok = ApplyChapterPatch(filepath.string(), 0,
                                    "## Chapter 1: Start\n\nNew text without marker.\n\n");
        std::string onDisk;
        { std::ifstream f(filepath);
          onDisk.assign(std::istreambuf_iterator<char>(f), {}); }

        bool hasMarker = onDisk.find("<!-- ch:0 -->") != std::string::npos;
        bool hasNew    = onDisk.find("New text without marker") != std::string::npos;
        bool hasKeep   = onDisk.find("Keep this") != std::string::npos;

        if (!ok || !hasMarker || !hasNew || !hasKeep) {
            std::cerr << "FAIL [apply-chapter-patch-restores-marker]: ok=" << ok
                      << " hasMarker=" << hasMarker
                      << " hasNew=" << hasNew
                      << " hasKeep=" << hasKeep << "\n";
            ++failures;
        } else {
            std::cout << "PASS [apply-chapter-patch-restores-marker]\n";
        }
        fs::remove_all(tmp);
    }

    // ReplaceChapter writes entirely new content to a file.
    {
        auto tmp = make_temp_dir();
        fs::path filepath = tmp / "ch.md";
        { std::ofstream f(filepath); f << "old content\n"; }

        bool ok = ReplaceChapter(filepath.string(), "# New\n\nNew content.\n");
        std::string onDisk;
        { std::ifstream f(filepath);
          onDisk.assign(std::istreambuf_iterator<char>(f), {}); }

        bool hasNew = onDisk.find("New content") != std::string::npos;
        bool noOld  = onDisk.find("old content") == std::string::npos;
        if (!ok || !hasNew || !noOld) {
            std::cerr << "FAIL [replace-chapter]: ok=" << ok
                      << " hasNew=" << hasNew
                      << " noOld=" << noOld << "\n";
            ++failures;
        } else {
            std::cout << "PASS [replace-chapter]\n";
        }
        fs::remove_all(tmp);
    }

    // ApplyFileOrder uses saved order first and appends new files alphabetically.
    {
        std::vector<std::string> files = {"b.md", "a.md", "c.md", "d.md"};
        std::vector<std::string> saved = {"c.md", "missing.md", "a.md"};
        auto ordered = ApplyFileOrder(files, saved);
        bool ok = ordered.size() == 4
               && ordered[0] == "c.md"
               && ordered[1] == "a.md"
               && ordered[2] == "b.md"
               && ordered[3] == "d.md";
        if (!ok) {
            std::cerr << "FAIL [apply-file-order]\n";
            ++failures;
        } else {
            std::cout << "PASS [apply-file-order]\n";
        }
    }

    // SaveFileOrder and LoadFileOrder persist a per-project order file.
    {
        auto tmp = make_temp_dir();
        std::vector<std::string> files = {"first.md", "second.md"};
        bool saved = SaveFileOrder(tmp.string(), files);
        auto loaded = LoadFileOrder(tmp.string());
        bool ok = saved && loaded == files && fs::exists(tmp / ".file_order");
        if (!ok) {
            std::cerr << "FAIL [file-order-roundtrip]\n";
            ++failures;
        } else {
            std::cout << "PASS [file-order-roundtrip]\n";
        }
        fs::remove_all(tmp);
    }

    // Refresh selection keeps the same file selected so version history remains visible.
    {
        std::vector<std::string> files = {"a.md", "b.md", "c.md"};
        int kept = RefreshedFileSelectionIndex(files, "c.md");
        int fallback = RefreshedFileSelectionIndex(files, "missing.md");
        int none = RefreshedFileSelectionIndex({}, "c.md");
        bool ok = kept == 2 && fallback == 0 && none == -1;
        if (!ok) {
            std::cerr << "FAIL [refresh-selection-index]: kept=" << kept
                      << " fallback=" << fallback
                      << " none=" << none << "\n";
            ++failures;
        } else {
            std::cout << "PASS [refresh-selection-index]\n";
        }
    }

    return failures;
}
