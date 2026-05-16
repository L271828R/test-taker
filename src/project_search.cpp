#include "project_search.h"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static std::string lowerAscii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
        out.push_back((char)std::tolower(c));
    return out;
}

bool ProjectSearchTextMatches(const std::string& searchableText,
                              const std::string& query) {
    std::string h = lowerAscii(searchableText);
    std::istringstream terms(lowerAscii(query));
    std::string term;
    bool sawTerm = false;
    while (terms >> term) {
        sawTerm = true;
        if (h.find(term) == std::string::npos)
            return false;
    }
    return sawTerm;
}

std::string BuildProjectSearchText(const std::string& name,
                                   const std::string& path,
                                   const std::string& source,
                                   const std::string& lastLLM) {
    std::string searchable = name + " " + source + " " + lastLLM;

    // Index only the file that activating the project would open — the first
    // .md alphabetically excluding claude.md. This keeps project search
    // consistent with what Ctrl+F can actually find in the view.
    std::error_code ec;
    std::vector<fs::path> mdFiles;
    for (auto& entry : fs::directory_iterator(path, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".md")
            continue;
        if (entry.path().filename() == "claude.md") continue;
        mdFiles.push_back(entry.path());
    }
    std::sort(mdFiles.begin(), mdFiles.end());
    if (!mdFiles.empty()) {
        std::ifstream f(mdFiles.front());
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            searchable += " " + mdFiles.front().filename().string() + " " + ss.str();
        }
    }

    return searchable;
}

bool ProjectMatchesSearch(const std::string& name,
                          const std::string& path,
                          const std::string& source,
                          const std::string& lastLLM,
                          const std::string& query) {
    if (query.empty()) return true;
    return ProjectSearchTextMatches(
        BuildProjectSearchText(name, path, source, lastLLM), query);
}
