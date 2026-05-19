#pragma once
#include <string>
#include <vector>

struct GitHubTreeInfo {
    std::string repoUrl;  // e.g. "https://github.com/google/googletest"
    std::string branch;   // e.g. "main"
    std::string subDir;   // e.g. "googletest/samples" (empty = repo root)
};

// Parse a GitHub tree URL into its components.
// Returns empty fields if the URL is not a recognisable GitHub tree URL.
// E.g. "https://github.com/google/googletest/tree/main/googletest/samples"
GitHubTreeInfo ParseGitHubTreeURL(const std::string& url);

// Recursively collect files under dir whose extension matches any in extensions.
// Pass an empty extensions vector to accept every file.
// Hidden files and directories (names starting with '.') are always skipped.
// Results are returned in sorted order.
std::vector<std::string> CollectFiles(const std::string& dir,
                                      const std::vector<std::string>& extensions);

// Shell out: git clone --depth=1 -b branch url destDir
// Returns true on success, populates err on failure.
bool GitClone(const std::string& url, const std::string& branch,
              const std::string& destDir, std::string& err);
