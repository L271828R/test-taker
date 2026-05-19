#include "project_panel.h"
#include "config.h"
#include "project.h"
#include "logger.h"
#include "meta.h"
#include "project_search.h"
#include <wx/choice.h>
#include <wx/dirdlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/textdlg.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Widget / event IDs
// ---------------------------------------------------------------------------
enum {
    ID_PP_TREE        = wxID_HIGHEST + 400,
    ID_PP_SEARCH,
    ID_PP_SORT,
    ID_PP_ACTIVATE,
    ID_PP_RENAME,
    ID_PP_REFRESH,
    ID_PP_SET_FOLDER,
    ID_PP_NEW_SUBFOLDER,
    ID_PP_NEW_PROJECT
};

enum class SortOrder { Name, Created, Modified };

wxBEGIN_EVENT_TABLE(ProjectPanel, wxPanel)
    EVT_TEXT(ID_PP_SEARCH,                  ProjectPanel::OnSearchChanged)
    EVT_CHOICE(ID_PP_SORT,                  ProjectPanel::OnSortChanged)
    EVT_BUTTON(ID_PP_ACTIVATE,              ProjectPanel::OnActivateBtn)
    EVT_BUTTON(ID_PP_RENAME,               ProjectPanel::OnRenameBtn)
    EVT_BUTTON(ID_PP_NEW_PROJECT,           ProjectPanel::OnNewProjectBtn)
    EVT_BUTTON(ID_PP_NEW_SUBFOLDER,         ProjectPanel::OnNewSubfolder)
    EVT_BUTTON(ID_PP_REFRESH,              ProjectPanel::OnRefreshBtn)
    EVT_BUTTON(ID_PP_SET_FOLDER,           ProjectPanel::OnSetFolderBtn)
    EVT_TREE_SEL_CHANGED(ID_PP_TREE,       ProjectPanel::OnTreeSelChanged)
    EVT_TREE_ITEM_ACTIVATED(ID_PP_TREE,    ProjectPanel::OnTreeItemActivated)
    EVT_TREE_ITEM_EXPANDING(ID_PP_TREE,    ProjectPanel::OnTreeExpanding)
    EVT_TREE_ITEM_COLLAPSING(ID_PP_TREE,   ProjectPanel::OnTreeCollapsing)
    EVT_TREE_BEGIN_DRAG(ID_PP_TREE,        ProjectPanel::OnTreeBeginDrag)
    EVT_TREE_END_DRAG(ID_PP_TREE,          ProjectPanel::OnTreeEndDrag)
wxEND_EVENT_TABLE()

// ===========================================================================
// Static helpers (preserved from original implementation)
// ===========================================================================

static std::string fmtTs(const std::string& ts) {
    if (ts.size() < 16) return ts;
    return ts.substr(0, 10) + " " + ts.substr(11, 5);
}

static std::string fmtFileTime(fs::file_time_type ft) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t t = std::chrono::system_clock::to_time_t(sctp);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

static std::string modifiedTime(const std::string& projectPath) {
    std::error_code ec;
    auto ft = fs::last_write_time(projectPath, ec);
    return ec ? "" : fmtFileTime(ft);
}

static std::string fmtSecs(int s) {
    if (s < 60) return std::to_string(s) + "s";
    return std::to_string(s / 60) + "m " + std::to_string(s % 60) + "s";
}

static std::string lastLLMSummary(const ProjectMeta& meta) {
    if (meta.timings.empty()) return "";
    const auto& last = meta.timings.back();
    std::string out = last.operation + ": " + fmtSecs(last.durationSeconds);
    if (!last.topic.empty()) out += " - " + last.topic;
    return out;
}

static std::string buildStats(const std::string& projectPath) {
    auto meta = LoadProjectMeta(projectPath);
    if (meta.created.empty() && meta.lastOpened.empty() &&
        meta.source.empty() && meta.timings.empty()) return "";
    std::string out;
    if (!meta.created.empty())
        out += "Created: " + fmtTs(meta.created);
    if (!meta.lastOpened.empty())
        out += (out.empty() ? "" : "   ") +
               std::string("Opened: ") + fmtTs(meta.lastOpened);
    if (!meta.source.empty())
        out += (out.empty() ? "" : "   ") + std::string("Source: ") + meta.source;
    if (!meta.timings.empty()) {
        if (!out.empty()) out += "   ";
        out += "Last " + lastLLMSummary(meta);
    }
    return out;
}

static std::string sourceFromConfig(const std::string& projectPath) {
    ProjectConfig cfg = LoadConfig(projectPath);
    return cfg.llmBackend;
}

static bool validProjectName(const std::string& name) {
    return !name.empty() &&
           name.find('/') == std::string::npos &&
           name.find('\\') == std::string::npos &&
           name != "." && name != "..";
}

// Returns true when a project node should be visible given query.
static bool projectMatchesQuery(const std::string& name,
                                const std::string& path,
                                const std::string& query) {
    if (query.empty()) return true;
    ProjectMeta meta = LoadProjectMeta(path);
    return ProjectMatchesSearch(name, path, meta.source, lastLLMSummary(meta), query);
}

// ===========================================================================
// Constructor
// ===========================================================================

ProjectPanel::ProjectPanel(wxWindow* parent, OpenCallback onProjectActivated)
    : wxPanel(parent, wxID_ANY)
    , m_openCallback(std::move(onProjectActivated))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* inner = new wxBoxSizer(wxVERTICAL);

    inner->Add(new wxStaticText(this, wxID_ANY, "Available Projects:"), 0, wxBOTTOM, 6);

    {
        auto* searchRow = new wxBoxSizer(wxHORIZONTAL);
        m_searchCtrl = new wxTextCtrl(this, ID_PP_SEARCH, wxEmptyString,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxTE_PROCESS_ENTER);
        m_searchCtrl->SetHint("Search projects...");
        searchRow->Add(m_searchCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

        searchRow->Add(new wxStaticText(this, wxID_ANY, "Sort:"),
                       0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        wxArrayString sortLabels;
        sortLabels.Add("Name");
        sortLabels.Add("Created");
        sortLabels.Add("Modified");
        m_sortChoice = new wxChoice(this, ID_PP_SORT, wxDefaultPosition,
                                    wxDefaultSize, sortLabels);
        m_sortChoice->SetSelection(0);
        searchRow->Add(m_sortChoice, 0, wxALIGN_CENTER_VERTICAL);
        inner->Add(searchRow, 0, wxEXPAND | wxBOTTOM, 8);
    }

    m_treeCtrl = new wxTreeCtrl(this, ID_PP_TREE,
                                wxDefaultPosition, wxDefaultSize,
                                wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT |
                                wxTR_SINGLE | wxTR_LINES_AT_ROOT);
    inner->Add(m_treeCtrl, 1, wxEXPAND | wxBOTTOM, 10);

    m_projectPathLabel = new wxStaticText(this, wxID_ANY, "Select a project to see its path.");
    wxFont small = m_projectPathLabel->GetFont();
    small.SetPointSize(small.GetPointSize() - 1);
    m_projectPathLabel->SetFont(small);
    inner->Add(m_projectPathLabel, 0, wxBOTTOM, 4);

    m_statsLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_statsLabel->SetFont(small);
    inner->Add(m_statsLabel, 0, wxBOTTOM, 10);

    auto* btnRow = new wxBoxSizer(wxHORIZONTAL);

    m_activateBtn = new wxButton(this, ID_PP_ACTIVATE, "Activate Project");
    m_activateBtn->Disable();
    btnRow->Add(m_activateBtn, 0, wxRIGHT, 6);

    m_renameBtn = new wxButton(this, ID_PP_RENAME, "Rename");
    m_renameBtn->Disable();
    btnRow->Add(m_renameBtn, 0, wxRIGHT, 6);

    m_newProjectBtn = new wxButton(this, ID_PP_NEW_PROJECT, "New Project…");
    m_newProjectBtn->Disable();
    btnRow->Add(m_newProjectBtn, 0, wxRIGHT, 6);

    m_newSubfolderBtn = new wxButton(this, ID_PP_NEW_SUBFOLDER, "New Subfolder…");
    m_newSubfolderBtn->Disable();
    btnRow->Add(m_newSubfolderBtn, 0, wxRIGHT, 6);

    btnRow->AddStretchSpacer();

    m_setFolderBtn = new wxButton(this, ID_PP_SET_FOLDER, "Set Projects Folder…");
    btnRow->Add(m_setFolderBtn, 0, wxRIGHT, 6);

    btnRow->Add(new wxButton(this, ID_PP_REFRESH, "Refresh"), 0);

    inner->Add(btnRow, 0, wxEXPAND | wxBOTTOM, 10);

    outer->Add(inner, 1, wxEXPAND | wxALL, 14);
    SetSizer(outer);

    RefreshProjects();
}

// ===========================================================================
// Tree population
// ===========================================================================

bool ProjectPanel::PopulateTree(wxTreeItemId parentId,
                                const std::string& dirPath,
                                int depth,
                                const std::string& query,
                                int sortOrder)
{
    if (depth <= 0) return false;

    std::error_code ec;
    std::vector<fs::directory_entry> entries;
    for (auto& e : fs::directory_iterator(dirPath, ec))
        entries.push_back(e);

    std::sort(entries.begin(), entries.end(),
              [&](const fs::directory_entry& a, const fs::directory_entry& b) {
                  if (sortOrder == 1) {
                      // Created: use meta timestamp, fall back to name.
                      auto ma = LoadProjectMeta(a.path().string());
                      auto mb = LoadProjectMeta(b.path().string());
                      if (ma.created != mb.created) return ma.created < mb.created;
                  } else if (sortOrder == 2) {
                      // Modified: filesystem mtime.
                      std::error_code e1, e2;
                      auto ta = fs::last_write_time(a.path(), e1);
                      auto tb = fs::last_write_time(b.path(), e2);
                      if (!e1 && !e2 && ta != tb) return ta < tb;
                  }
                  return a.path().filename() < b.path().filename();
              });

    bool anyAdded = false;

    for (auto& entry : entries) {
        std::error_code ec2;
        if (!entry.is_directory(ec2)) continue;

        std::string childPath = entry.path().string();
        std::string childName = entry.path().filename().string();

        if (ProjectExists(childPath)) {
            // ---- Project node ----
            bool visible = query.empty() || projectMatchesQuery(childName, childPath, query);
            if (!visible) continue;

            std::string source = sourceFromConfig(childPath);
            EnsureProjectMeta(childPath, source);
            ProjectMeta meta = LoadProjectMeta(childPath);

            // Append a date annotation to the label based on sort order.
            std::string dateStr;
            if (sortOrder == 1 && !meta.created.empty())
                dateStr = "  [" + fmtTs(meta.created) + "]";
            else if (sortOrder == 2)
                dateStr = "  [" + modifiedTime(childPath) + "]";

            auto* data = new TreeNode();
            data->kind = TreeNode::Kind::Project;
            data->path = childPath;
            data->name = childName;

            wxString label = wxString::FromUTF8("\U0001F4C4 " + childName + dateStr);
            m_treeCtrl->AppendItem(parentId, label, -1, -1, data);
            anyAdded = true;
        } else {
            // ---- Folder node ----
            // We only add the folder if at least one descendant project matches.
            // To test this we add a temporary folder item, populate it, and
            // remove it if nothing was added under it.
            auto* data = new TreeNode();
            data->kind = TreeNode::Kind::Folder;
            data->path = childPath;
            data->name = childName;

            wxString label = wxString::FromUTF8("\U0001F4C1 " + childName);
            wxTreeItemId folderId = m_treeCtrl->AppendItem(parentId, label, -1, -1, data);

            bool childAdded = PopulateTree(folderId, childPath, depth - 1, query, sortOrder);
            if (!childAdded && !query.empty()) {
                // No matching descendants — hide the folder during search.
                m_treeCtrl->Delete(folderId);
            } else if (childAdded || query.empty()) {
                // Restore expand state.
                if (m_expandedPaths.count(childPath))
                    m_treeCtrl->Expand(folderId);
                anyAdded = true;
            }
        }
    }

    return anyAdded;
}

// ===========================================================================
// RefreshProjects
// ===========================================================================

void ProjectPanel::RefreshProjects() {
    m_treeCtrl->DeleteAllItems();
    m_activateBtn->Disable();
    m_renameBtn->Disable();
    m_newSubfolderBtn->Disable();
    m_projectPathLabel->SetLabel("Select a project to see its path.");
    m_statsLabel->SetLabel(wxEmptyString);

    AppConfig cfg = LoadConfig();
    if (cfg.defaultFolder.empty()) {
        m_projectPathLabel->SetLabel(
            "No projects folder set. Click \"Set Projects Folder…\" to get started.");
        CallAfter([this]() {
            wxCommandEvent dummy;
            OnSetFolderBtn(dummy);
        });
        return;
    }

    if (!fs::exists(cfg.defaultFolder)) {
        m_projectPathLabel->SetLabel(
            "Projects folder not found: " + cfg.defaultFolder +
            " — click \"Set Projects Folder…\" to choose a new one.");
        return;
    }

    // Create a hidden root; all real content hangs off it.
    wxTreeItemId root = m_treeCtrl->AddRoot("root");

    std::string query = m_searchCtrl ? m_searchCtrl->GetValue().ToStdString() : "";
    int sortOrder = m_sortChoice ? m_sortChoice->GetSelection() : 0;
    bool anyAdded = PopulateTree(root, cfg.defaultFolder, 4, query, sortOrder);

    // These buttons are usable once a root folder is configured.
    m_newSubfolderBtn->Enable(true);
    m_newProjectBtn->Enable(true);

    if (!anyAdded) {
        m_projectPathLabel->SetLabel(query.empty()
            ? "No projects found in this folder. Use \"New Subfolder…\" to organise, or create a project in the Create tab."
            : "No projects match the current search.");
    }

    // Try to re-select (and on first load, activate) the last-used project.
    AppState st = LoadAppState();
    if (!st.currentProject.empty()) {
        std::function<wxTreeItemId(wxTreeItemId)> findProject =
            [&](wxTreeItemId node) -> wxTreeItemId {
                wxTreeItemIdValue cookie;
                wxTreeItemId child = m_treeCtrl->GetFirstChild(node, cookie);
                while (child.IsOk()) {
                    auto* tn = dynamic_cast<TreeNode*>(m_treeCtrl->GetItemData(child));
                    if (tn && tn->kind == TreeNode::Kind::Project &&
                        tn->path == st.currentProject) {
                        return child;
                    }
                    wxTreeItemId found = findProject(child);
                    if (found.IsOk()) return found;
                    child = m_treeCtrl->GetNextChild(node, cookie);
                }
                return wxTreeItemId();
            };

        wxTreeItemId found = findProject(root);
        if (found.IsOk()) {
            m_treeCtrl->SelectItem(found);
            m_treeCtrl->EnsureVisible(found);

            // Only fire the activation callback once at startup, not on every
            // sort/search/refresh call. This also prevents a second instance from
            // auto-activating a different project that the first instance last used.
            if (!m_startupDone) {
                auto* tn = dynamic_cast<TreeNode*>(m_treeCtrl->GetItemData(found));
                if (tn && m_openCallback) {
                    std::string path = tn->path;
                    CallAfter([this, path]() { m_openCallback(path); });
                }
            }
        }
    }
    m_startupDone = true;
}

// ===========================================================================
// Selection helpers
// ===========================================================================

TreeNode* ProjectPanel::SelectedNode() const {
    wxTreeItemId sel = m_treeCtrl->GetSelection();
    if (!sel.IsOk()) return nullptr;
    return dynamic_cast<TreeNode*>(m_treeCtrl->GetItemData(sel));
}

// ===========================================================================
// Event handlers — tree
// ===========================================================================

void ProjectPanel::OnTreeSelChanged(wxTreeEvent&) {
    TreeNode* tn = SelectedNode();

    AppConfig cfg = LoadConfig();
    bool hasFolder = !cfg.defaultFolder.empty();
    m_newSubfolderBtn->Enable(hasFolder);
    m_newProjectBtn->Enable(hasFolder);

    if (!tn) {
        m_activateBtn->Disable();
        m_renameBtn->Disable();
        m_projectPathLabel->SetLabel("Select a project to see its path.");
        m_statsLabel->SetLabel(wxEmptyString);
        return;
    }

    m_projectPathLabel->SetLabel(wxString::FromUTF8(tn->path));

    if (tn->kind == TreeNode::Kind::Project) {
        m_activateBtn->Enable();
        m_renameBtn->Enable();
        m_statsLabel->SetLabel(wxString::FromUTF8(buildStats(tn->path)));
    } else {
        m_activateBtn->Disable();
        m_renameBtn->Enable();
        m_statsLabel->SetLabel(wxEmptyString);
    }
    Layout();
}

void ProjectPanel::OnTreeItemActivated(wxTreeEvent&) {
    TreeNode* tn = SelectedNode();
    if (!tn) return;
    if (tn->kind == TreeNode::Kind::Project)
        ActivateSelectedProject();
    else
        m_treeCtrl->Toggle(m_treeCtrl->GetSelection());
}

void ProjectPanel::OnTreeExpanding(wxTreeEvent& evt) {
    wxTreeItemId item = evt.GetItem();
    auto* tn = dynamic_cast<TreeNode*>(m_treeCtrl->GetItemData(item));
    if (tn) m_expandedPaths.insert(tn->path);
}

void ProjectPanel::OnTreeCollapsing(wxTreeEvent& evt) {
    wxTreeItemId item = evt.GetItem();
    auto* tn = dynamic_cast<TreeNode*>(m_treeCtrl->GetItemData(item));
    if (tn) m_expandedPaths.erase(tn->path);
}

void ProjectPanel::OnTreeBeginDrag(wxTreeEvent& evt) {
    // Only allow dragging project or folder nodes (not the hidden root).
    TreeNode* tn = dynamic_cast<TreeNode*>(m_treeCtrl->GetItemData(evt.GetItem()));
    if (!tn) { evt.Veto(); return; }
    m_dragItem = evt.GetItem();
    evt.Allow();   // must call Allow() to start the drag
}

void ProjectPanel::OnTreeEndDrag(wxTreeEvent& evt) {
    wxTreeItemId target = evt.GetItem();
    if (!m_dragItem.IsOk() || !target.IsOk() || target == m_dragItem) {
        m_dragItem = wxTreeItemId();
        return;
    }

    auto* src = dynamic_cast<TreeNode*>(m_treeCtrl->GetItemData(m_dragItem));
    auto* dst = dynamic_cast<TreeNode*>(m_treeCtrl->GetItemData(target));
    m_dragItem = wxTreeItemId();

    if (!src || !dst) return;

    // Only drop onto a folder node.
    if (dst->kind != TreeNode::Kind::Folder) {
        wxMessageBox("Drop onto a folder to move a project or subfolder into it.",
                     "Move", wxOK | wxICON_INFORMATION, this);
        return;
    }

    // Prevent moving a folder into one of its own descendants.
    if (dst->path.rfind(src->path, 0) == 0 &&
        (dst->path.size() == src->path.size() ||
         dst->path[src->path.size()] == '/')) {
        wxMessageBox("Cannot move a folder into itself or one of its subfolders.",
                     "Move", wxOK | wxICON_WARNING, this);
        return;
    }

    fs::path srcPath(src->path);
    fs::path dstPath = fs::path(dst->path) / srcPath.filename();

    if (fs::exists(dstPath)) {
        wxMessageBox("A folder named \"" + srcPath.filename().string() +
                     "\" already exists in \"" + dst->name + "\".",
                     "Move", wxOK | wxICON_WARNING, this);
        return;
    }

    std::error_code ec;
    fs::rename(srcPath, dstPath, ec);
    if (ec) {
        wxMessageBox(wxString::FromUTF8("Could not move: " + ec.message()),
                     "Move", wxOK | wxICON_ERROR, this);
        return;
    }

    // Update expand tracking: old path → new path.
    if (m_expandedPaths.erase(src->path))
        m_expandedPaths.insert(dstPath.string());

    // Keep the destination folder expanded so the moved item is visible.
    m_expandedPaths.insert(dst->path);

    Logger::get().log("Moved: " + src->path + " -> " + dstPath.string());
    RefreshProjects();
}

// ===========================================================================
// Event handlers — buttons
// ===========================================================================

void ProjectPanel::OnSearchChanged(wxCommandEvent&) {
    RefreshProjects();
}

void ProjectPanel::OnSortChanged(wxCommandEvent&) {
    RefreshProjects();
}

void ProjectPanel::OnActivateBtn(wxCommandEvent&) {
    ActivateSelectedProject();
}

void ProjectPanel::OnRenameBtn(wxCommandEvent&) {
    TreeNode* tn = SelectedNode();
    if (!tn) return;

    std::string oldName = tn->name;
    std::string oldPath = tn->path;
    bool isProject = (tn->kind == TreeNode::Kind::Project);

    wxString title = isProject ? "Rename Project" : "Rename Folder";
    wxString entered = wxGetTextFromUser(
        "Enter a new name:", title,
        wxString::FromUTF8(oldName), this).Trim();
    if (entered.empty()) return;

    std::string newName = entered.ToStdString();
    if (newName == oldName) return;
    if (!validProjectName(newName)) {
        wxMessageBox("Use a folder-safe name without slashes.",
                     "Invalid Name", wxOK | wxICON_WARNING, this);
        return;
    }

    fs::path oldFsPath(oldPath);
    fs::path newFsPath = oldFsPath.parent_path() / newName;
    if (fs::exists(newFsPath)) {
        wxMessageBox("A folder with that name already exists.",
                     title, wxOK | wxICON_WARNING, this);
        return;
    }

    std::error_code ec;
    fs::rename(oldFsPath, newFsPath, ec);
    if (ec) {
        wxMessageBox(wxString::FromUTF8("Could not rename: " + ec.message()),
                     title, wxOK | wxICON_ERROR, this);
        return;
    }

    if (isProject) {
        AppState st = LoadAppState();
        if (st.currentProject == oldPath) {
            st.currentProject = newFsPath.string();
            SaveAppState(st);
        }
    }

    // Update expand-path set if the renamed path was tracked.
    if (m_expandedPaths.erase(oldPath))
        m_expandedPaths.insert(newFsPath.string());

    Logger::get().log("Renamed: " + oldPath + " -> " + newFsPath.string());
    RefreshProjects();
}

void ProjectPanel::OnNewProjectBtn(wxCommandEvent&) {
    AppConfig cfg = LoadConfig();
    if (cfg.defaultFolder.empty()) {
        wxMessageBox("No projects folder is set. Click \"Set Projects Folder…\" first.",
                     "New Project", wxOK | wxICON_WARNING, this);
        return;
    }

    // Determine parent: selected folder → inside it, otherwise root.
    std::string parentPath;
    TreeNode* tn = SelectedNode();
    if (tn && tn->kind == TreeNode::Kind::Folder)
        parentPath = tn->path;
    else
        parentPath = cfg.defaultFolder;

    wxString entered = wxGetTextFromUser(
        wxString::FromUTF8("Enter a name for the new project.\nWill be created in: " + parentPath),
        "New Project", wxEmptyString, this).Trim();
    if (entered.empty()) return;

    std::string name = entered.ToStdString();
    if (!validProjectName(name)) {
        wxMessageBox("Use a folder-safe name without slashes.",
                     "Invalid Name", wxOK | wxICON_WARNING, this);
        return;
    }

    if (fs::exists(fs::path(parentPath) / name)) {
        wxMessageBox("A folder with that name already exists.",
                     "New Project", wxOK | wxICON_WARNING, this);
        return;
    }

    if (!CreateProject(parentPath, name)) {
        wxMessageBox("Could not create project in:\n" + parentPath,
                     "New Project", wxOK | wxICON_ERROR, this);
        return;
    }

    m_expandedPaths.insert(parentPath);
    Logger::get().log("Created project: " + (fs::path(parentPath) / name).string());
    RefreshProjects();
}

void ProjectPanel::OnNewSubfolder(wxCommandEvent&) {
    // Determine where to create the subfolder.
    // Selected folder → inside it.  Anything else → root projects folder.
    std::string parentPath;
    TreeNode* tn = SelectedNode();
    if (tn && tn->kind == TreeNode::Kind::Folder) {
        parentPath = tn->path;
    } else {
        AppConfig cfg = LoadConfig();
        parentPath = cfg.defaultFolder;
    }

    if (parentPath.empty()) {
        wxMessageBox("No projects folder is set.", "New Subfolder",
                     wxOK | wxICON_WARNING, this);
        return;
    }

    wxString prompt = wxString::FromUTF8(
        "Enter a name for the new subfolder.\nWill be created in: " + parentPath);
    wxString entered = wxGetTextFromUser(prompt, "New Subfolder",
                                         wxEmptyString, this).Trim();
    if (entered.empty()) return;

    std::string folderName = entered.ToStdString();
    if (!validProjectName(folderName)) {
        wxMessageBox("Use a folder-safe name without slashes.",
                     "Invalid Name", wxOK | wxICON_WARNING, this);
        return;
    }

    fs::path newDir = fs::path(parentPath) / folderName;
    if (fs::exists(newDir)) {
        wxMessageBox("A folder with that name already exists.",
                     "New Subfolder", wxOK | wxICON_WARNING, this);
        return;
    }

    std::error_code ec;
    fs::create_directory(newDir, ec);
    if (ec) {
        wxMessageBox(wxString::FromUTF8("Could not create folder: " + ec.message()),
                     "New Subfolder", wxOK | wxICON_ERROR, this);
        return;
    }

    // Keep the parent expanded so the new folder is immediately visible.
    m_expandedPaths.insert(parentPath);
    Logger::get().log("Created subfolder: " + newDir.string());
    RefreshProjects();
}

void ProjectPanel::OnRefreshBtn(wxCommandEvent&) {
    RefreshProjects();
}

void ProjectPanel::OnSetFolderBtn(wxCommandEvent&) {
    AppConfig current = LoadConfig();
    wxString defaultPath = current.defaultFolder.empty()
        ? wxString(getenv("HOME") ?: "")
        : wxString::FromUTF8(current.defaultFolder);

    wxDirDialog dlg(this, "Choose your projects folder", defaultPath,
                    wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() == wxID_CANCEL) return;

    std::string chosen = dlg.GetPath().ToStdString();

    const char* home = getenv("HOME");
    if (!home) return;
    std::string configDir = std::string(home) + "/.config/test-taker";
    std::string configPath = configDir + "/config";

    fs::create_directories(configDir);

    std::string existing;
    {
        std::ifstream fin(configPath);
        if (fin) {
            std::ostringstream ss;
            ss << fin.rdbuf();
            existing = ss.str();
        }
    }

    // Strip any existing defaultFolder line and rewrite.
    std::string updated;
    std::istringstream ss(existing);
    std::string line;
    while (std::getline(ss, line)) {
        std::string key = line.substr(0, line.find('='));
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
            key.pop_back();
        if (key != "defaultFolder")
            updated += line + "\n";
    }
    updated += "defaultFolder = " + chosen + "\n";

    {
        std::ofstream fout(configPath);
        if (!fout) {
            wxMessageBox("Could not write config file:\n" + configPath,
                         "Error", wxOK | wxICON_ERROR, this);
            return;
        }
        fout << updated;
    }

    Logger::get().log("Set defaultFolder to: " + chosen);
    RefreshProjects();
}

// ===========================================================================
// ActivateSelectedProject
// ===========================================================================

void ProjectPanel::ActivateSelectedProject() {
    TreeNode* tn = SelectedNode();
    if (!tn || tn->kind != TreeNode::Kind::Project) return;

    std::string projectName = tn->name;
    std::string projectPath = tn->path;

    AppState st = LoadAppState();
    st.currentProject = projectPath;
    SaveAppState(st);

    Logger::get().log("Activating project: " + projectName);
    RecordOpen(projectPath);

    if (m_openCallback)
        m_openCallback(projectPath);
}
