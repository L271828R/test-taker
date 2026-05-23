// tests/test_project_panel_html.cpp
// Tests for BuildProjectPanelHTML — no wxWidgets dependency.

#include "project_panel_html.h"
#include <iostream>
#include <string>

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static ProjectPanelState emptyState() {
    ProjectPanelState s;
    s.hasFolder = true;
    return s;
}

int test_project_panel_html() {
    int failures = 0;

    // ── [pp-html-search-input] ────────────────────────────────────────────────
    {
        std::string html = BuildProjectPanelHTML(emptyState());
        bool ok = contains(html, "id='pp-search'") || contains(html, "id=\"pp-search\"");
        if (!ok) {
            std::cerr << "FAIL [pp-html-search-input]: no pp-search input found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-search-input]\n";
        }
    }

    // ── [pp-html-sort-select] ─────────────────────────────────────────────────
    {
        std::string html = BuildProjectPanelHTML(emptyState());
        bool hasSel     = contains(html, "id='pp-sort'") || contains(html, "id=\"pp-sort\"");
        bool hasName    = contains(html, "Name");
        bool hasCreated = contains(html, "Created");
        bool hasModified= contains(html, "Modified");
        bool ok = hasSel && hasName && hasCreated && hasModified;
        if (!ok) {
            std::cerr << "FAIL [pp-html-sort-select]: sel=" << hasSel
                      << " Name=" << hasName << " Created=" << hasCreated
                      << " Modified=" << hasModified << "\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-sort-select]\n";
        }
    }

    // ── [pp-html-activate-btn] ────────────────────────────────────────────────
    {
        std::string html = BuildProjectPanelHTML(emptyState());
        bool ok = contains(html, "ppActivate") || contains(html, "Activate");
        if (!ok) {
            std::cerr << "FAIL [pp-html-activate-btn]: no Activate button found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-activate-btn]\n";
        }
    }

    // ── [pp-html-rename-btn] ──────────────────────────────────────────────────
    {
        std::string html = BuildProjectPanelHTML(emptyState());
        bool ok = contains(html, "ppRename") || contains(html, "Rename");
        if (!ok) {
            std::cerr << "FAIL [pp-html-rename-btn]: no Rename button found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-rename-btn]\n";
        }
    }

    // ── [pp-html-delete-btn] ──────────────────────────────────────────────────
    {
        std::string html = BuildProjectPanelHTML(emptyState());
        bool ok = contains(html, "ppDelete") || contains(html, "Delete");
        if (!ok) {
            std::cerr << "FAIL [pp-html-delete-btn]: no Delete button found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-delete-btn]\n";
        }
    }

    // ── [pp-html-new-project-btn] ─────────────────────────────────────────────
    {
        std::string html = BuildProjectPanelHTML(emptyState());
        bool ok = contains(html, "ppNewProject") || contains(html, "New Project");
        if (!ok) {
            std::cerr << "FAIL [pp-html-new-project-btn]: no New Project button found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-new-project-btn]\n";
        }
    }

    // ── [pp-html-set-folder-btn] ──────────────────────────────────────────────
    {
        std::string html = BuildProjectPanelHTML(emptyState());
        bool ok = contains(html, "ppSetFolder") || contains(html, "Set Folder");
        if (!ok) {
            std::cerr << "FAIL [pp-html-set-folder-btn]: no Set Folder button found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-set-folder-btn]\n";
        }
    }

    // ── [pp-html-refresh-btn] ─────────────────────────────────────────────────
    {
        std::string html = BuildProjectPanelHTML(emptyState());
        bool ok = contains(html, "ppRefresh") || contains(html, "Refresh");
        if (!ok) {
            std::cerr << "FAIL [pp-html-refresh-btn]: no Refresh button found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-refresh-btn]\n";
        }
    }

    // ── [pp-html-info-div] ────────────────────────────────────────────────────
    {
        std::string html = BuildProjectPanelHTML(emptyState());
        bool ok = contains(html, "id='pp-info'") || contains(html, "id=\"pp-info\"");
        if (!ok) {
            std::cerr << "FAIL [pp-html-info-div]: no pp-info div found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-info-div]\n";
        }
    }

    // ── [pp-html-ppaction-fn] ─────────────────────────────────────────────────
    {
        std::string html = BuildProjectPanelHTML(emptyState());
        bool ok = contains(html, "ppAction");
        if (!ok) {
            std::cerr << "FAIL [pp-html-ppaction-fn]: ppAction function not found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-ppaction-fn]\n";
        }
    }

    // ── [pp-html-no-folder-msg] ───────────────────────────────────────────────
    {
        ProjectPanelState s;
        s.hasFolder = false;
        s.folderMsg = "No projects folder set.";
        std::string html = BuildProjectPanelHTML(s);
        bool hasMsg  = contains(html, "No projects folder set.");
        // No <li class='pp-proj'> or <li class='pp-folder'> list items rendered
        bool noProjItem   = !contains(html, "<li class='pp-proj'") &&
                            !contains(html, "<li class=\"pp-proj\"");
        bool noFolderItem = !contains(html, "<li class='pp-folder'") &&
                            !contains(html, "<li class=\"pp-folder\"");
        bool ok = hasMsg && noProjItem && noFolderItem;
        if (!ok) {
            std::cerr << "FAIL [pp-html-no-folder-msg]: msg=" << hasMsg
                      << " noProjItem=" << noProjItem
                      << " noFolderItem=" << noFolderItem << "\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-no-folder-msg]\n";
        }
    }

    // ── [pp-html-project-rendered] ────────────────────────────────────────────
    {
        ProjectPanelState s;
        s.hasFolder = true;
        ProjectEntry proj;
        proj.path    = "/home/user/projects/MyProj";
        proj.name    = "MyProj";
        proj.isFolder = false;
        s.tree.push_back(proj);
        std::string html = BuildProjectPanelHTML(s);
        bool ok = contains(html, "/home/user/projects/MyProj");
        if (!ok) {
            std::cerr << "FAIL [pp-html-project-rendered]: data-path not found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-project-rendered]\n";
        }
    }

    // ── [pp-html-folder-rendered] ─────────────────────────────────────────────
    {
        ProjectPanelState s;
        s.hasFolder = true;
        ProjectEntry folder;
        folder.path    = "/home/user/projects/MyFolder";
        folder.name    = "MyFolder";
        folder.isFolder = true;
        s.tree.push_back(folder);
        std::string html = BuildProjectPanelHTML(s);
        bool ok = contains(html, "pp-folder");
        if (!ok) {
            std::cerr << "FAIL [pp-html-folder-rendered]: pp-folder class not found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-folder-rendered]\n";
        }
    }

    // ── [pp-html-active-project] ──────────────────────────────────────────────
    {
        ProjectPanelState s;
        s.hasFolder  = true;
        s.activePath = "/home/user/projects/MyProj";
        ProjectEntry proj;
        proj.path    = "/home/user/projects/MyProj";
        proj.name    = "MyProj";
        proj.isFolder = false;
        s.tree.push_back(proj);
        std::string html = BuildProjectPanelHTML(s);
        // The active item should have the "active" CSS class alongside pp-proj
        bool ok = contains(html, "active");
        if (!ok) {
            std::cerr << "FAIL [pp-html-active-project]: active class not found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-active-project]\n";
        }
    }

    // ── [pp-html-stats-attr] ──────────────────────────────────────────────────
    {
        ProjectPanelState s;
        s.hasFolder = true;
        ProjectEntry proj;
        proj.path    = "/home/user/projects/MyProj";
        proj.name    = "MyProj";
        proj.stats   = "Created: 2024-01-01";
        proj.isFolder = false;
        s.tree.push_back(proj);
        std::string html = BuildProjectPanelHTML(s);
        bool ok = contains(html, "data-stats") && contains(html, "Created: 2024-01-01");
        if (!ok) {
            std::cerr << "FAIL [pp-html-stats-attr]: data-stats or stats text not found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-stats-attr]\n";
        }
    }

    // ── [pp-html-search-js] ───────────────────────────────────────────────────
    {
        std::string html = BuildProjectPanelHTML(emptyState());
        bool ok = contains(html, "ppSearch");
        if (!ok) {
            std::cerr << "FAIL [pp-html-search-js]: ppSearch function not found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-search-js]\n";
        }
    }

    // ── [pp-html-drag-drop-js] ────────────────────────────────────────────────
    {
        ProjectPanelState s;
        s.hasFolder = true;
        ProjectEntry proj;
        proj.path    = "/home/user/projects/MyProj";
        proj.name    = "MyProj";
        proj.isFolder = false;
        s.tree.push_back(proj);
        std::string html = BuildProjectPanelHTML(s);
        bool ok = contains(html, "draggable") || contains(html, "dragstart") ||
                  contains(html, "ppDragStart");
        if (!ok) {
            std::cerr << "FAIL [pp-html-drag-drop-js]: drag-drop not found\n";
            ++failures;
        } else {
            std::cout << "PASS [pp-html-drag-drop-js]\n";
        }
    }

    return failures;
}
