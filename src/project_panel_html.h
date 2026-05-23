#pragma once
#include <set>
#include <string>
#include <vector>

struct ProjectEntry {
    std::string path;
    std::string name;
    std::string dateStr;               // date annotation for sort display (may be empty)
    std::string stats;                 // stats string shown on selection
    bool        isFolder  = false;
    std::vector<ProjectEntry> children;
};

struct ProjectPanelState {
    std::vector<ProjectEntry> tree;    // top-level entries
    std::string activePath;            // path of currently active project (highlighted)
    std::string selectedPath;          // path of selected (clicked) item
    std::string folderMsg;             // shown in path/status area when nothing selected
    std::string sortOrder  = "name";   // "name" | "created" | "modified"
    std::set<std::string> expandedPaths;
    bool        hasFolder  = false;    // false = no projects folder configured
    bool        darkMode   = false;
};

// Build the full HTML page for the Project Panel.
// Pure function — no wxWidgets dependency, directly unit-testable.
std::string BuildProjectPanelHTML(const ProjectPanelState& s);
