#include "project_search.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static void write_file(const fs::path& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

int test_project_search() {
    int failures = 0;

    // Plain text search is not fuzzy: "stream" must appear as text.
    {
        bool ok = ProjectSearchTextMatches("a project about streaming data", "stream")
               && !ProjectSearchTextMatches("s t r e a m letters spread out", "stream");
        if (!ok) {
            std::cerr << "FAIL [project-search-not-fuzzy]\n";
            ++failures;
        } else {
            std::cout << "PASS [project-search-not-fuzzy]\n";
        }
    }

    // Multiple words require all terms, case-insensitively.
    {
        bool ok = ProjectSearchTextMatches("Kafka Stream processing notes", "stream kafka")
               && ProjectSearchTextMatches("Kafka Stream processing notes", "STREAM")
               && !ProjectSearchTextMatches("Kafka notes", "stream kafka");
        if (!ok) {
            std::cerr << "FAIL [project-search-all-terms]\n";
            ++failures;
        } else {
            std::cout << "PASS [project-search-all-terms]\n";
        }
    }

    // Project search indexes the first .md file (alphabetically) and its content.
    {
        auto dir = fs::temp_directory_path() / "st_project_search";
        fs::remove_all(dir);
        fs::create_directories(dir);
        write_file(dir / "chapter.md", "# Pipes\n\nThis chapter discusses stream flow.");
        write_file(dir / "notes.txt", "stream should not be found from txt");

        bool ok = ProjectMatchesSearch("water", dir.string(), "Codex CLI", "",
                                       "stream flow")
               && ProjectMatchesSearch("water", dir.string(), "Codex CLI", "",
                                       "chapter.md")
               && !ProjectMatchesSearch("water", dir.string(), "Codex CLI", "",
                                        "unrelated");
        if (!ok) {
            std::cerr << "FAIL [project-search-md-content]\n";
            ++failures;
        } else {
            std::cout << "PASS [project-search-md-content]\n";
        }
        fs::remove_all(dir);
    }

    // Only the first .md alphabetically is indexed — not all files.
    // "alpha.md" comes before "zebra.md"; a word only in zebra.md must not match.
    {
        auto dir = fs::temp_directory_path() / "st_search_first_only";
        fs::remove_all(dir);
        fs::create_directories(dir);
        write_file(dir / "alpha.md", "# Alpha\n\nThis talks about rivers.");
        write_file(dir / "zebra.md", "# Zebra\n\nThis talks about mountains.");

        bool ok = ProjectMatchesSearch("myproject", dir.string(), "", "", "rivers")
               && !ProjectMatchesSearch("myproject", dir.string(), "", "", "mountains");
        if (!ok) {
            std::cerr << "FAIL [project-search-first-file-only]\n";
            ++failures;
        } else {
            std::cout << "PASS [project-search-first-file-only]\n";
        }
        fs::remove_all(dir);
    }

    // claude.md is excluded from indexing even when it is alphabetically first.
    {
        auto dir = fs::temp_directory_path() / "st_search_no_claude";
        fs::remove_all(dir);
        fs::create_directories(dir);
        write_file(dir / "claude.md",  "project context: this is a secret prompt");
        write_file(dir / "chapter.md", "# Story\n\nOnce upon a time.");

        bool secretNotFound = !ProjectMatchesSearch("myproject", dir.string(), "", "",
                                                    "secret prompt");
        bool storyFound     =  ProjectMatchesSearch("myproject", dir.string(), "", "",
                                                    "once upon a time");
        if (!secretNotFound || !storyFound) {
            std::cerr << "FAIL [project-search-excludes-claude-md]\n";
            ++failures;
        } else {
            std::cout << "PASS [project-search-excludes-claude-md]\n";
        }
        fs::remove_all(dir);
    }

    // Project name and LLM source are still searchable.
    {
        auto dir = fs::temp_directory_path() / "st_search_metadata";
        fs::remove_all(dir);
        fs::create_directories(dir);
        write_file(dir / "ch01.md", "# Chapter\n\nSome content here.");

        bool ok = ProjectMatchesSearch("MyStory", dir.string(), "Anthropic API", "",
                                       "mystory")
               && ProjectMatchesSearch("MyStory", dir.string(), "Anthropic API", "",
                                       "anthropic")
               && !ProjectMatchesSearch("MyStory", dir.string(), "Anthropic API", "",
                                        "openai");
        if (!ok) {
            std::cerr << "FAIL [project-search-metadata]\n";
            ++failures;
        } else {
            std::cout << "PASS [project-search-metadata]\n";
        }
        fs::remove_all(dir);
    }

    // A project with a matching word only in a non-first file is NOT matched —
    // this is the exact scenario that was reported as a bug.
    {
        auto dir = fs::temp_directory_path() / "st_search_ctrlf_consistency";
        fs::remove_all(dir);
        fs::create_directories(dir);
        // ch01 is opened in the view; ch02 is not. "build" is only in ch02.
        write_file(dir / "ch01.md", "# Chapter 1\n\nThis chapter has no special word.");
        write_file(dir / "ch02.md", "# Chapter 2\n\nThis chapter mentions building things.");

        bool ch01Open_noMatch = !ProjectMatchesSearch("myproject", dir.string(), "", "",
                                                      "building");
        if (!ch01Open_noMatch) {
            std::cerr << "FAIL [project-search-ctrlf-consistency]\n";
            ++failures;
        } else {
            std::cout << "PASS [project-search-ctrlf-consistency]\n";
        }
        fs::remove_all(dir);
    }

    return failures;
}
