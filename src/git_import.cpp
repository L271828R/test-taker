#include "git_import.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
GitHubTreeInfo ParseGitHubTreeURL(const std::string& url) {
    // Expected form: https://github.com/<owner>/<repo>[/tree/<branch>[/<path>]]
    const std::string ghPrefix = "https://github.com/";
    if (url.rfind(ghPrefix, 0) != 0) return {};

    std::string rest = url.substr(ghPrefix.size());

    // Split on '/'
    auto split = [](const std::string& s, char delim) {
        std::vector<std::string> parts;
        std::string cur;
        for (char c : s) {
            if (c == delim) { parts.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) parts.push_back(cur);
        return parts;
    };

    auto parts = split(rest, '/');
    if (parts.size() < 2) return {};

    GitHubTreeInfo info;
    info.repoUrl = ghPrefix + parts[0] + "/" + parts[1];

    // parts[2] should be "tree", parts[3] the branch, parts[4..] the subdir
    if (parts.size() >= 4 && parts[2] == "tree") {
        info.branch = parts[3];
        for (size_t i = 4; i < parts.size(); ++i) {
            if (!info.subDir.empty()) info.subDir += '/';
            info.subDir += parts[i];
        }
    }

    return info;
}

// ---------------------------------------------------------------------------
std::vector<std::string> CollectFiles(const std::string& dir,
                                      const std::vector<std::string>& extensions) {
    std::vector<std::string> results;
    std::error_code ec;

    for (auto it = fs::recursive_directory_iterator(dir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }

        // Skip hidden directories (don't descend into them).
        if (it->is_directory(ec)) {
            if (!it->path().filename().string().empty() &&
                it->path().filename().string()[0] == '.') {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (!it->is_regular_file(ec)) continue;

        const std::string name = it->path().filename().string();
        // Skip hidden files.
        if (!name.empty() && name[0] == '.') continue;

        if (extensions.empty()) {
            results.push_back(it->path().string());
        } else {
            std::string ext = it->path().extension().string();
            // Normalise extension to lowercase for comparison.
            for (char& c : ext) c = static_cast<char>(std::tolower((unsigned char)c));
            for (const auto& want : extensions) {
                if (ext == want) { results.push_back(it->path().string()); break; }
            }
        }
    }

    std::sort(results.begin(), results.end());
    return results;
}

// ---------------------------------------------------------------------------
bool GitClone(const std::string& url, const std::string& branch,
              const std::string& destDir, std::string& err) {
    std::string branchArg = branch.empty() ? "" : ("-b \"" + branch + "\" ");
    std::string cmd = "git clone --depth=1 " + branchArg
                    + "\"" + url + "\" "
                    + "\"" + destDir + "\" 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { err = "popen failed"; return false; }

    std::string output;
    std::array<char, 1024> buf;
    while (fgets(buf.data(), buf.size(), pipe))
        output += buf.data();

    int status = pclose(pipe);
    if (status != 0) {
        err = output.empty() ? "git clone failed" : output;
        return false;
    }
    return true;
}
