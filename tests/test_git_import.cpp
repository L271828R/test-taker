#include "git_import.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static void touch(const fs::path& p, const std::string& content = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << content;
}

int test_git_import() {
    int failures = 0;

    // ParseGitHubTreeURL: extracts repo, branch, subdir from a GitHub tree URL
    {
        auto info = ParseGitHubTreeURL(
            "https://github.com/google/googletest/tree/main/googletest/samples");
        bool ok = info.repoUrl  == "https://github.com/google/googletest"
               && info.branch   == "main"
               && info.subDir   == "googletest/samples";
        if (!ok) {
            std::cerr << "FAIL [parse-github-tree]: repo='" << info.repoUrl
                      << "' branch='" << info.branch
                      << "' subdir='" << info.subDir << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-github-tree]\n";
        }
    }

    // ParseGitHubTreeURL: returns empty on non-GitHub URL
    {
        auto info = ParseGitHubTreeURL("https://example.com/some/page");
        bool ok = info.repoUrl.empty() && info.branch.empty() && info.subDir.empty();
        if (!ok) {
            std::cerr << "FAIL [parse-github-tree-non-gh]\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-github-tree-non-gh]\n";
        }
    }

    // ParseGitHubTreeURL: plain repo URL (no tree path) sets subDir empty
    {
        auto info = ParseGitHubTreeURL("https://github.com/google/googletest");
        bool ok = info.repoUrl == "https://github.com/google/googletest"
               && info.subDir.empty();
        if (!ok) {
            std::cerr << "FAIL [parse-github-tree-no-subdir]: repo='" << info.repoUrl
                      << "' subdir='" << info.subDir << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-github-tree-no-subdir]\n";
        }
    }

    // CollectFiles: finds files matching extensions, skips hidden dirs
    {
        auto tmp = fs::temp_directory_path() / "tt_git_import_collect";
        fs::remove_all(tmp);
        touch(tmp / "a.cc",          "code");
        touch(tmp / "b.h",           "header");
        touch(tmp / "c.txt",         "text");
        touch(tmp / "ignore.exe",    "binary");
        touch(tmp / "sub" / "d.cc",  "code2");
        touch(tmp / ".git" / "HEAD", "ref");   // hidden dir — must be skipped

        auto files = CollectFiles(tmp.string(), {".cc", ".h", ".txt"});

        bool hasA   = false, hasB = false, hasC = false, hasD = false;
        bool hasExe = false, hasGit = false;
        for (const auto& f : files) {
            if (f.find("a.cc")   != std::string::npos) hasA = true;
            if (f.find("b.h")    != std::string::npos) hasB = true;
            if (f.find("c.txt")  != std::string::npos) hasC = true;
            if (f.find("d.cc")   != std::string::npos) hasD = true;
            if (f.find(".exe")   != std::string::npos) hasExe = true;
            if (f.find(".git")   != std::string::npos) hasGit = true;
        }

        bool ok = hasA && hasB && hasC && hasD && !hasExe && !hasGit;
        if (!ok) {
            std::cerr << "FAIL [collect-files]: a=" << hasA << " b=" << hasB
                      << " c=" << hasC << " d=" << hasD
                      << " exe=" << hasExe << " git=" << hasGit << "\n";
            ++failures;
        } else {
            std::cout << "PASS [collect-files]\n";
        }
        fs::remove_all(tmp);
    }

    // CollectFiles: empty extensions accepts all non-hidden files
    {
        auto tmp = fs::temp_directory_path() / "tt_git_import_all";
        fs::remove_all(tmp);
        touch(tmp / "readme.md");
        touch(tmp / "data.json");
        touch(tmp / ".hidden");
        touch(tmp / ".git" / "config");

        auto files = CollectFiles(tmp.string(), {});

        bool hasReadme = false, hasJson = false, hasHidden = false, hasGit = false;
        for (const auto& f : files) {
            if (f.find("readme.md")  != std::string::npos) hasReadme = true;
            if (f.find("data.json")  != std::string::npos) hasJson   = true;
            if (f.find(".hidden")    != std::string::npos) hasHidden  = true;
            if (f.find(".git")       != std::string::npos) hasGit    = true;
        }

        bool ok = hasReadme && hasJson && !hasHidden && !hasGit;
        if (!ok) {
            std::cerr << "FAIL [collect-files-all-ext]: readme=" << hasReadme
                      << " json=" << hasJson << " hidden=" << hasHidden
                      << " git=" << hasGit << "\n";
            ++failures;
        } else {
            std::cout << "PASS [collect-files-all-ext]\n";
        }
        fs::remove_all(tmp);
    }

    // CollectFiles: files are returned in sorted order
    {
        auto tmp = fs::temp_directory_path() / "tt_git_import_sort";
        fs::remove_all(tmp);
        touch(tmp / "z.txt");
        touch(tmp / "a.txt");
        touch(tmp / "m.txt");

        auto files = CollectFiles(tmp.string(), {".txt"});
        bool sorted = files.size() == 3
                   && fs::path(files[0]).filename() <= fs::path(files[1]).filename()
                   && fs::path(files[1]).filename() <= fs::path(files[2]).filename();
        if (!sorted) {
            std::cerr << "FAIL [collect-files-sorted]\n";
            ++failures;
        } else {
            std::cout << "PASS [collect-files-sorted]\n";
        }
        fs::remove_all(tmp);
    }

    return failures;
}
