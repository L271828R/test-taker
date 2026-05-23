#include "project_panel.h"
#include "config.h"
#include "project.h"
#include "logger.h"
#include "meta.h"
#include "project_search.h"
#include <wx/dirdlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Event table
// ---------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ProjectPanel, wxPanel)
    EVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED(wxID_ANY, ProjectPanel::OnPpAction)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// JSON field extractor (same pattern as nsJsonField / chatJsonField)
// ---------------------------------------------------------------------------

static std::string ppJsonField(const std::string& json, const std::string& key) {
    std::string kq = "\"" + key + "\":";
    auto pos = json.find(kq);
    if (pos == std::string::npos) return "";
    pos += kq.size();
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        ++pos;
        std::string val;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos;
                switch (json[pos]) {
                    case 'n': val += '\n'; break;
                    case 't': val += '\t'; break;
                    case '"': val += '"'; break;
                    case '\\': val += '\\'; break;
                    default: val += json[pos]; break;
                }
            } else {
                val += json[pos];
            }
            ++pos;
        }
        return val;
    } else {
        size_t end = json.find_first_of(",}", pos);
        if (end == std::string::npos) end = json.size();
        return json.substr(pos, end - pos);
    }
}

// ---------------------------------------------------------------------------
// Static helpers (preserved from original)
// ---------------------------------------------------------------------------

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

static bool projectMatchesQuery(const std::string& name,
                                const std::string& path,
                                const std::string& query) {
    if (query.empty()) return true;
    ProjectMeta meta = LoadProjectMeta(path);
    return ProjectMatchesSearch(name, path, meta.source, lastLLMSummary(meta), query);
}

// ---------------------------------------------------------------------------
// BuildTree helpers — recursive scan producing ProjectEntry tree
// ---------------------------------------------------------------------------

static std::string dateStrFor(const std::string& sortOrder,
                               const std::string& childPath,
                               const ProjectMeta& meta) {
    if (sortOrder == "created" && !meta.created.empty())
        return "  [" + fmtTs(meta.created) + "]";
    if (sortOrder == "modified")
        return "  [" + modifiedTime(childPath) + "]";
    return "";
}

static bool compareEntries(const fs::directory_entry& a,
                            const fs::directory_entry& b,
                            const std::string& sortOrder) {
    if (sortOrder == "created") {
        auto ma = LoadProjectMeta(a.path().string());
        auto mb = LoadProjectMeta(b.path().string());
        if (ma.created != mb.created) return ma.created < mb.created;
    } else if (sortOrder == "modified") {
        std::error_code e1, e2;
        auto ta = fs::last_write_time(a.path(), e1);
        auto tb = fs::last_write_time(b.path(), e2);
        if (!e1 && !e2 && ta != tb) return ta < tb;
    }
    return a.path().filename() < b.path().filename();
}

// Returns true if at least one project was added.
static bool PopulateEntries(std::vector<ProjectEntry>& out,
                             const std::string& dirPath,
                             int depth,
                             const std::string& query,
                             const std::string& sortOrder)
{
    if (depth <= 0) return false;

    std::error_code ec;
    std::vector<fs::directory_entry> entries;
    for (auto& e : fs::directory_iterator(dirPath, ec))
        entries.push_back(e);

    std::sort(entries.begin(), entries.end(),
              [&](const fs::directory_entry& a, const fs::directory_entry& b) {
                  return compareEntries(a, b, sortOrder);
              });

    bool anyAdded = false;

    for (auto& entry : entries) {
        std::error_code ec2;
        if (!entry.is_directory(ec2)) continue;

        std::string childPath = entry.path().string();
        std::string childName = entry.path().filename().string();

        if (ProjectExists(childPath)) {
            bool visible = query.empty() ||
                           projectMatchesQuery(childName, childPath, query);
            if (!visible) continue;

            std::string source = sourceFromConfig(childPath);
            EnsureProjectMeta(childPath, source);
            ProjectMeta meta = LoadProjectMeta(childPath);

            ProjectEntry proj;
            proj.path     = childPath;
            proj.name     = childName;
            proj.isFolder = false;
            proj.dateStr  = dateStrFor(sortOrder, childPath, meta);
            proj.stats    = buildStats(childPath);
            out.push_back(std::move(proj));
            anyAdded = true;
        } else {
            ProjectEntry folder;
            folder.path     = childPath;
            folder.name     = childName;
            folder.isFolder = true;

            bool childAdded = PopulateEntries(folder.children, childPath,
                                              depth - 1, query, sortOrder);

            if (!childAdded && !query.empty()) {
                // No matching descendants — skip folder during search
                continue;
            }
            out.push_back(std::move(folder));
            anyAdded = true;
        }
    }

    return anyAdded;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ProjectPanel::ProjectPanel(wxWindow* parent, OpenCallback onProjectActivated)
    : wxPanel(parent, wxID_ANY)
    , m_openCallback(std::move(onProjectActivated))
{
    m_webView = wxWebView::New(this, wxID_ANY);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_webView, 1, wxEXPAND);
    SetSizer(sizer);

    m_webView->SetPage("<html><body></body></html>", "");

    CallAfter([this]() {
        m_webView->AddScriptMessageHandler("ppAction");
    });

    BuildTree();
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void ProjectPanel::Render() {
    std::string html = BuildProjectPanelHTML(m_state);
    m_webView->SetPage(wxString::FromUTF8(html), "");
}

// ---------------------------------------------------------------------------
// RefreshProjects (public)
// ---------------------------------------------------------------------------

void ProjectPanel::RefreshProjects() {
    BuildTree();
}

// ---------------------------------------------------------------------------

void ProjectPanel::SetDarkMode(bool dark) {
    m_state.darkMode = dark;
    Render();
}

// ---------------------------------------------------------------------------
// BuildTree — populate m_state.tree from filesystem, then Render
// ---------------------------------------------------------------------------

void ProjectPanel::BuildTree() {
    AppConfig cfg = LoadConfig();

    m_state.tree.clear();
    m_state.hasFolder = !cfg.defaultFolder.empty();

    if (cfg.defaultFolder.empty()) {
        m_state.folderMsg =
            "No projects folder set. Click \"Set Folder\xe2\x80\xa6\" to get started.";
        Render();
        // Offer folder picker on first launch
        if (!m_startupDone) {
            m_startupDone = true;
            CallAfter([this]() { HandleSetFolder(); });
        }
        return;
    }

    if (!fs::exists(cfg.defaultFolder)) {
        m_state.folderMsg =
            "Projects folder not found: " + cfg.defaultFolder +
            " \xe2\x80\x94 click \"Set Folder\xe2\x80\xa6\" to choose a new one.";
        Render();
        m_startupDone = true;
        return;
    }

    bool anyAdded = PopulateEntries(m_state.tree, cfg.defaultFolder,
                                    4, "", m_state.sortOrder);

    if (!anyAdded) {
        m_state.folderMsg =
            "No projects found. Use \"New Subfolder\xe2\x80\xa6\" to organise, "
            "or create a project in the Create tab.";
    } else {
        m_state.folderMsg = cfg.defaultFolder;
    }

    // Restore active project from app state
    AppState st = LoadAppState();
    m_state.activePath = st.currentProject;

    Render();

    // Auto-activate last-used project on first load
    if (!m_startupDone && !m_state.activePath.empty()) {
        std::string path = m_state.activePath;
        CallAfter([this, path]() {
            if (m_openCallback) m_openCallback(path);
        });
    }
    m_startupDone = true;
}

// ---------------------------------------------------------------------------
// HandleActivate
// ---------------------------------------------------------------------------

void ProjectPanel::HandleActivate(const std::string& path) {
    if (path.empty()) return;

    AppState st = LoadAppState();
    st.currentProject = path;
    SaveAppState(st);

    m_state.activePath  = path;
    m_state.selectedPath = path;

    std::string name = fs::path(path).filename().string();
    Logger::get().log("Activating project: " + name);
    RecordOpen(path);

    if (m_openCallback) m_openCallback(path);
    Render();
}

// ---------------------------------------------------------------------------
// HandleRename
// ---------------------------------------------------------------------------

void ProjectPanel::HandleRename(const std::string& path) {
    if (path.empty()) return;

    bool isProject = ProjectExists(path);
    std::string oldName = fs::path(path).filename().string();
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

    fs::path oldFsPath(path);
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
        if (st.currentProject == path) {
            st.currentProject = newFsPath.string();
            SaveAppState(st);
        }
    }

    if (m_state.expandedPaths.erase(path))
        m_state.expandedPaths.insert(newFsPath.string());
    if (m_state.activePath  == path) m_state.activePath  = newFsPath.string();
    if (m_state.selectedPath == path) m_state.selectedPath = newFsPath.string();

    Logger::get().log("Renamed: " + path + " -> " + newFsPath.string());
    BuildTree();
}

// ---------------------------------------------------------------------------
// HandleDelete
// ---------------------------------------------------------------------------

void ProjectPanel::HandleDelete(const std::string& path) {
    if (!ProjectExists(path)) return;

    AppConfig cfg = LoadConfig();
    if (!IsProjectDeletable(path, cfg.defaultFolder)) {
        wxMessageBox("This project cannot be deleted — it is outside the projects folder.",
                     "Delete Project", wxOK | wxICON_WARNING, this);
        return;
    }

    std::string name = fs::path(path).filename().string();
    wxString msg = wxString::FromUTF8(
        "Permanently delete \"" + name + "\" and all its files?\n\nThis cannot be undone.");
    if (wxMessageBox(msg, "Delete Project",
                     wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES)
        return;

    std::error_code ec;
    fs::remove_all(fs::path(path), ec);
    if (ec) {
        wxMessageBox(wxString::FromUTF8("Could not delete: " + ec.message()),
                     "Delete Project", wxOK | wxICON_ERROR, this);
        return;
    }

    AppState st = LoadAppState();
    bool wasActive = (st.currentProject == path);
    if (wasActive) {
        st.currentProject = "";
        SaveAppState(st);
        m_state.activePath = "";
        if (m_openCallback) m_openCallback("");
    }
    if (m_state.selectedPath == path) m_state.selectedPath = "";

    Logger::get().log("Deleted project: " + path);
    BuildTree();
}

// ---------------------------------------------------------------------------
// HandleNewProject
// ---------------------------------------------------------------------------

void ProjectPanel::HandleNewProject(const std::string& parentPath) {
    AppConfig cfg = LoadConfig();
    if (cfg.defaultFolder.empty()) {
        wxMessageBox("No projects folder is set. Click \"Set Folder\xe2\x80\xa6\" first.",
                     "New Project", wxOK | wxICON_WARNING, this);
        return;
    }

    std::string parent = parentPath.empty() ? cfg.defaultFolder : parentPath;

    wxString entered = wxGetTextFromUser(
        wxString::FromUTF8("Enter a name for the new project.\nWill be created in: " + parent),
        "New Project", wxEmptyString, this).Trim();
    if (entered.empty()) return;

    std::string name = entered.ToStdString();
    if (!validProjectName(name)) {
        wxMessageBox("Use a folder-safe name without slashes.",
                     "Invalid Name", wxOK | wxICON_WARNING, this);
        return;
    }

    if (fs::exists(fs::path(parent) / name)) {
        wxMessageBox("A folder with that name already exists.",
                     "New Project", wxOK | wxICON_WARNING, this);
        return;
    }

    if (!CreateProject(parent, name)) {
        wxMessageBox("Could not create project in:\n" + parent,
                     "New Project", wxOK | wxICON_ERROR, this);
        return;
    }

    m_state.expandedPaths.insert(parent);
    Logger::get().log("Created project: " + (fs::path(parent) / name).string());
    BuildTree();
}

// ---------------------------------------------------------------------------
// HandleNewSubfolder
// ---------------------------------------------------------------------------

void ProjectPanel::HandleNewSubfolder(const std::string& parentPath) {
    AppConfig cfg = LoadConfig();
    std::string parent = parentPath.empty() ? cfg.defaultFolder : parentPath;

    if (parent.empty()) {
        wxMessageBox("No projects folder is set.", "New Subfolder",
                     wxOK | wxICON_WARNING, this);
        return;
    }

    wxString prompt = wxString::FromUTF8(
        "Enter a name for the new subfolder.\nWill be created in: " + parent);
    wxString entered = wxGetTextFromUser(
        prompt, "New Subfolder", wxEmptyString, this).Trim();
    if (entered.empty()) return;

    std::string folderName = entered.ToStdString();
    if (!validProjectName(folderName)) {
        wxMessageBox("Use a folder-safe name without slashes.",
                     "Invalid Name", wxOK | wxICON_WARNING, this);
        return;
    }

    fs::path newDir = fs::path(parent) / folderName;
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

    m_state.expandedPaths.insert(parent);
    Logger::get().log("Created subfolder: " + newDir.string());
    BuildTree();
}

// ---------------------------------------------------------------------------
// HandleSetFolder
// ---------------------------------------------------------------------------

void ProjectPanel::HandleSetFolder() {
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
    std::string configDir  = std::string(home) + "/.config/test-taker";
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
    BuildTree();
}

// ---------------------------------------------------------------------------
// HandleMove
// ---------------------------------------------------------------------------

void ProjectPanel::HandleMove(const std::string& src, const std::string& dst) {
    if (src.empty() || dst.empty() || src == dst) return;

    // Only drop onto a folder node
    if (ProjectExists(dst)) {
        wxMessageBox("Drop onto a folder to move a project or subfolder into it.",
                     "Move", wxOK | wxICON_INFORMATION, this);
        return;
    }

    // Prevent moving a folder into one of its own descendants
    if (dst.rfind(src, 0) == 0 &&
        (dst.size() == src.size() || dst[src.size()] == '/')) {
        wxMessageBox("Cannot move a folder into itself or one of its subfolders.",
                     "Move", wxOK | wxICON_WARNING, this);
        return;
    }

    fs::path srcPath(src);
    fs::path dstPath = fs::path(dst) / srcPath.filename();

    if (fs::exists(dstPath)) {
        wxMessageBox("A folder named \"" + srcPath.filename().string() +
                     "\" already exists in the destination.",
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

    if (m_state.expandedPaths.erase(src))
        m_state.expandedPaths.insert(dstPath.string());
    m_state.expandedPaths.insert(dst);

    if (m_state.activePath  == src) m_state.activePath  = dstPath.string();
    if (m_state.selectedPath == src) m_state.selectedPath = dstPath.string();

    Logger::get().log("Moved: " + src + " -> " + dstPath.string());
    BuildTree();
}

// ---------------------------------------------------------------------------
// HandleSort
// ---------------------------------------------------------------------------

void ProjectPanel::HandleSort(const std::string& order) {
    m_state.sortOrder = order;
    BuildTree();
}

// ---------------------------------------------------------------------------
// OnPpAction — dispatch incoming JS messages
// ---------------------------------------------------------------------------

void ProjectPanel::OnPpAction(wxWebViewEvent& evt) {
    std::string payload = evt.GetString().ToStdString();
    std::string action  = ppJsonField(payload, "action");

    if      (action == "activate")     HandleActivate(ppJsonField(payload, "path"));
    else if (action == "rename")       HandleRename(ppJsonField(payload, "path"));
    else if (action == "delete")       HandleDelete(ppJsonField(payload, "path"));
    else if (action == "newProject")   HandleNewProject(ppJsonField(payload, "parentPath"));
    else if (action == "newSubfolder") HandleNewSubfolder(ppJsonField(payload, "parentPath"));
    else if (action == "setFolder")    HandleSetFolder();
    else if (action == "move")         HandleMove(ppJsonField(payload, "src"),
                                                  ppJsonField(payload, "dst"));
    else if (action == "sort")         HandleSort(ppJsonField(payload, "order"));
    else if (action == "refresh")      BuildTree();
}
