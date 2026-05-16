#include "edit_panel.h"
#include "config.h"
#include "creator.h"
#include "editor.h"
#include "git_ops.h"
#include "llm.h"
#include "logger.h"
#include "meta.h"
#include "project.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cctype>
#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/choice.h>
#include <wx/dataobj.h>
#include <wx/listbox.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

namespace fs = std::filesystem;

static std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    out += "'";
    return out;
}

static std::string applescript_string(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    out += "\"";
    return out;
}

static LLMConfig llm_config_from_state(const AppState& st) {
    LLMConfig cfg;
    cfg.backend = LLMBackend::Clipboard;
    cfg.backend = BackendFromLabel(st.backend);
    if (!st.apiKey.empty())      cfg.apiKey      = st.apiKey;
    if (!st.ollamaModel.empty()) cfg.ollamaModel = st.ollamaModel;
    return cfg;
}

static std::string language_suffix(const std::string& language) {
    std::string out;
    for (unsigned char c : language) {
        if (std::isalnum(c)) out += (char)std::tolower(c);
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "translated" : out;
}

static wxString preview_label(const char* prefix, int id, const std::string& preview) {
    wxString text = wxString::FromUTF8(preview);
    if (text.length() > 80) text = text.Left(80) + "...";
    return wxString::Format("%s:%d  ", prefix, id) + text;
}

enum {
    ID_EP_REFRESH        = wxID_HIGHEST + 300,
    ID_EP_CHAPTER,
    ID_EP_OPEN_VIEW,
    ID_EP_OPEN_VIM,
    ID_EP_MOVE_UP,
    ID_EP_MOVE_DOWN,
    ID_EP_RENAME_FILE,
    ID_EP_DELETE_FILE,
    ID_EP_REWRITE,
    ID_EP_TRANSLATE,
    ID_EP_RADIO_TIDBIT,
    ID_EP_RADIO_CHAPTER,
    ID_EP_COMMIT,
    ID_EP_VIEW_VER,
    ID_EP_DIFF,
    ID_EP_RESTORE,
    ID_EP_CHECKOUT,
    ID_EP_STASH,
    ID_EP_UNSTASH,
};

wxBEGIN_EVENT_TABLE(EditPanel, wxPanel)
    EVT_BUTTON(ID_EP_REFRESH,           EditPanel::OnRefresh)
    EVT_LISTBOX(ID_EP_CHAPTER,          EditPanel::OnChapterSelected)
    EVT_LISTBOX_DCLICK(ID_EP_CHAPTER,   EditPanel::OnChapterActivated)
    EVT_BUTTON(ID_EP_MOVE_UP,           EditPanel::OnMoveFileUp)
    EVT_BUTTON(ID_EP_MOVE_DOWN,         EditPanel::OnMoveFileDown)
    EVT_BUTTON(ID_EP_RENAME_FILE,       EditPanel::OnRenameFile)
    EVT_BUTTON(ID_EP_DELETE_FILE,       EditPanel::OnDeleteFile)
    EVT_RADIOBUTTON(ID_EP_RADIO_TIDBIT,  EditPanel::OnTargetChanged)
    EVT_RADIOBUTTON(ID_EP_RADIO_CHAPTER, EditPanel::OnTargetChanged)
    EVT_BUTTON(ID_EP_REWRITE,           EditPanel::OnRewrite)
    EVT_BUTTON(ID_EP_TRANSLATE,         EditPanel::OnTranslate)
    EVT_BUTTON(ID_EP_COMMIT,            EditPanel::OnCommit)
    EVT_BUTTON(ID_EP_VIEW_VER,          EditPanel::OnViewVersion)
    EVT_BUTTON(ID_EP_DIFF,              EditPanel::OnDiff)
    EVT_BUTTON(ID_EP_RESTORE,           EditPanel::OnRestore)
    EVT_BUTTON(ID_EP_CHECKOUT,          EditPanel::OnCheckout)
    EVT_BUTTON(ID_EP_STASH,             EditPanel::OnStash)
    EVT_BUTTON(ID_EP_UNSTASH,           EditPanel::OnUnstash)
wxEND_EVENT_TABLE()

EditPanel::EditPanel(wxWindow* parent, OpenCallback onFileChanged)
    : wxPanel(parent, wxID_ANY)
    , m_openCallback(std::move(onFileChanged))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* inner = new wxBoxSizer(wxVERTICAL);

    // ── File list (left) + right list (tidbits or chapter sections) ──────
    {
        auto* cols = new wxBoxSizer(wxHORIZONTAL);

        auto* leftCol = new wxBoxSizer(wxVERTICAL);
        leftCol->Add(new wxStaticText(this, wxID_ANY, "File:"), 0, wxBOTTOM, 4);
        m_chapterList = new wxListBox(this, ID_EP_CHAPTER,
                                      wxDefaultPosition, wxSize(310, 110));
        leftCol->Add(m_chapterList, 1, wxEXPAND);

        auto* orderRow = new wxBoxSizer(wxHORIZONTAL);
        m_moveUpBtn = new wxButton(this, ID_EP_MOVE_UP, "↑",
                                   wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_moveDownBtn = new wxButton(this, ID_EP_MOVE_DOWN, "↓",
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        orderRow->Add(m_moveUpBtn, 0, wxRIGHT, 6);
        orderRow->Add(m_moveDownBtn, 0);
        leftCol->Add(orderRow, 0, wxTOP, 6);

        auto* openRow = new wxBoxSizer(wxHORIZONTAL);
        openRow->Add(new wxStaticText(this, wxID_ANY, "Open:"),
                     0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        m_radioOpenView = new wxRadioButton(this, ID_EP_OPEN_VIEW, "View tab",
                                            wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_radioOpenVim  = new wxRadioButton(this, ID_EP_OPEN_VIM, "Vim");
        m_radioOpenView->SetValue(true);
        openRow->Add(m_radioOpenView, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        openRow->Add(m_radioOpenVim,  0, wxALIGN_CENTER_VERTICAL);
        leftCol->Add(openRow, 0, wxTOP, 6);

        auto* fileBtnRow = new wxBoxSizer(wxHORIZONTAL);
        m_renameFileBtn = new wxButton(this, ID_EP_RENAME_FILE, "Rename",
                                       wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_deleteFileBtn = new wxButton(this, ID_EP_DELETE_FILE, "Delete",
                                       wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        fileBtnRow->Add(m_renameFileBtn, 0, wxRIGHT, 6);
        fileBtnRow->Add(m_deleteFileBtn, 0);
        leftCol->Add(fileBtnRow, 0, wxTOP, 6);

        cols->Add(leftCol, 0, wxEXPAND | wxRIGHT, 12);

        auto* rightCol = new wxBoxSizer(wxVERTICAL);
        m_rightLabel = new wxStaticText(this, wxID_ANY, "Tidbits:");
        rightCol->Add(m_rightLabel, 0, wxBOTTOM, 4);
        m_tidbitList = new wxListBox(this, wxID_ANY,
                                     wxDefaultPosition, wxSize(-1, 70));
        rightCol->Add(m_tidbitList, 0, wxEXPAND);
        cols->Add(rightCol, 1, wxEXPAND);

        inner->Add(cols, 0, wxEXPAND | wxBOTTOM, 6);

        auto* refreshRow = new wxBoxSizer(wxHORIZONTAL);
        refreshRow->AddStretchSpacer();
        refreshRow->Add(new wxButton(this, ID_EP_REFRESH, "↺ Refresh",
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
        inner->Add(refreshRow, 0, wxEXPAND | wxBOTTOM, 10);
    }
    inner->Add(new wxStaticLine(this), 0, wxEXPAND | wxBOTTOM, 10);

    // ── Rewrite target ────────────────────────────────────────────────────
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, "Rewrite:"),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        m_radioTidbit  = new wxRadioButton(this, ID_EP_RADIO_TIDBIT, "Selected tidbit",
                                           wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_radioChapter = new wxRadioButton(this, ID_EP_RADIO_CHAPTER, "Selected chapter");
        m_radioTidbit->SetValue(true);
        row->Add(m_radioTidbit,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
        row->Add(m_radioChapter, 0, wxALIGN_CENTER_VERTICAL);
        inner->Add(row, 0, wxBOTTOM, 10);
    }

    // ── Instruction ───────────────────────────────────────────────────────
    inner->Add(new wxStaticText(this, wxID_ANY, "Instruction:"), 0, wxBOTTOM, 6);
    m_instructCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                    wxDefaultPosition, wxSize(-1, 60),
                                    wxTE_MULTILINE | wxTE_RICH2 | wxTE_WORDWRAP);
    m_instructCtrl->SetHint("e.g. Make this shorter and funnier, translate to Spanish…");
    inner->Add(m_instructCtrl, 0, wxEXPAND | wxBOTTOM, 8);

    m_rewriteBtn = new wxButton(this, ID_EP_REWRITE, "Rewrite");
    inner->Add(m_rewriteBtn, 0, wxBOTTOM, 10);
    inner->Add(new wxStaticLine(this), 0, wxEXPAND | wxBOTTOM, 10);

    // ── Translate selected file ───────────────────────────────────────────
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, "Translate file to:"),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        m_translateLangCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                             wxDefaultPosition, wxSize(180, -1));
        m_translateLangCtrl->SetHint("e.g. Spanish, Chinese");
        row->Add(m_translateLangCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        m_translateBtn = new wxButton(this, ID_EP_TRANSLATE, "Translate file",
                                      wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        row->Add(m_translateBtn, 0, wxALIGN_CENTER_VERTICAL);
        inner->Add(row, 0, wxEXPAND | wxBOTTOM, 10);
    }
    inner->Add(new wxStaticLine(this), 0, wxEXPAND | wxBOTTOM, 10);

    // ── Git version history ───────────────────────────────────────────────
    inner->Add(new wxStaticText(this, wxID_ANY, "Version history for this project folder (select 1 or 2):"),
               0, wxBOTTOM, 4);

    // History list — LB_EXTENDED lets user ctrl/shift-click two items.
    m_historyList = new wxListBox(this, wxID_ANY,
                                  wxDefaultPosition, wxSize(-1, 110),
                                  0, nullptr, wxLB_EXTENDED);
    inner->Add(m_historyList, 0, wxEXPAND | wxBOTTOM, 6);

    {
        auto* box = new wxStaticBoxSizer(wxVERTICAL, this, "Commit");
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        m_commitMsgCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                         wxDefaultPosition, wxDefaultSize, 0);
        m_commitMsgCtrl->SetHint("Commit message…");
        row->Add(m_commitMsgCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        m_commitBtn = new wxButton(this, ID_EP_COMMIT, "Save to git",
                                   wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        row->Add(m_commitBtn, 0, wxALIGN_CENTER_VERTICAL);
        box->Add(row, 0, wxEXPAND | wxALL, 6);
        inner->Add(box, 0, wxEXPAND | wxBOTTOM, 8);
    }

    {
        auto* box = new wxStaticBoxSizer(wxVERTICAL, this, "Version");
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        m_viewVerBtn = new wxButton(this, ID_EP_VIEW_VER, "View version",
                                    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_diffBtn    = new wxButton(this, ID_EP_DIFF,    "Diff selected vs current",
                                    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_restoreBtn = new wxButton(this, ID_EP_RESTORE, "Restore",
                                    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_checkoutBtn = new wxButton(this, ID_EP_CHECKOUT, "Checkout",
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        row->Add(m_viewVerBtn, 0, wxRIGHT, 8);
        row->Add(m_diffBtn,    0, wxRIGHT, 8);
        row->Add(m_restoreBtn, 0, wxRIGHT, 8);
        row->Add(m_checkoutBtn, 0);
        box->Add(row, 0, wxEXPAND | wxALL, 6);
        inner->Add(box, 0, wxEXPAND | wxBOTTOM, 8);
    }

    {
        auto* box = new wxStaticBoxSizer(wxVERTICAL, this, "Stash");
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        m_stashBtn = new wxButton(this, ID_EP_STASH, "Stash changes",
                                  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_unstashBtn = new wxButton(this, ID_EP_UNSTASH, "Unstash latest",
                                    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        row->Add(m_stashBtn, 0, wxRIGHT, 8);
        row->Add(m_unstashBtn, 0);
        box->Add(row, 0, wxEXPAND | wxALL, 6);
        inner->Add(box, 0, wxEXPAND | wxBOTTOM, 10);
    }
    inner->Add(new wxStaticLine(this), 0, wxEXPAND | wxBOTTOM, 8);

    // ── Status ────────────────────────────────────────────────────────────
    m_statusCtrl = new wxTextCtrl(this, wxID_ANY, "Select a file to begin.",
                                  wxDefaultPosition, wxSize(-1, 70),
                                  wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
    inner->Add(m_statusCtrl, 1, wxEXPAND);

    outer->Add(inner, 1, wxEXPAND | wxALL, 14);
    SetSizer(outer);

    RefreshChapters();
}

// ---------------------------------------------------------------------------

std::string EditPanel::CurrentProjectPath() const {
    AppConfig  cfg = LoadConfig();
    AppState   st  = LoadAppState();
    if (cfg.defaultFolder.empty() || st.currentProject.empty()) return "";
    return cfg.defaultFolder + "/" + st.currentProject;
}

std::string EditPanel::CurrentChapterPath() const {
    int sel = m_chapterList->GetSelection();
    if (sel == wxNOT_FOUND) return "";
    std::string proj = CurrentProjectPath();
    if (proj.empty()) return "";
    return proj + "/" + m_chapterList->GetString(sel).ToStdString();
}

void EditPanel::RefreshChapters() {
    std::string previousFile;
    int previousSelection = m_chapterList->GetSelection();
    if (previousSelection != wxNOT_FOUND)
        previousFile = m_chapterList->GetString(previousSelection).ToStdString();

    m_chapterList->Clear();
    m_tidbitList->Clear();
    m_historyList->Clear();
    m_tidbits.clear();
    m_sections.clear();
    m_commits.clear();

    std::string proj = CurrentProjectPath();
    if (proj.empty()) { SetStatus("No project selected — use the Create tab first."); return; }

    std::error_code ec;
    std::vector<std::string> files;
    for (auto& e : fs::directory_iterator(proj, ec))
        if (e.path().extension() == ".md" && e.path().filename().string()[0] != '.')
            files.push_back(e.path().filename().string());
    std::sort(files.begin(), files.end());
    files = ApplyFileOrder(files, LoadFileOrder(proj));

    for (auto& f : files)
        m_chapterList->Append(wxString::FromUTF8(f));

    if (m_chapterList->GetCount() > 0) {
        int selection = RefreshedFileSelectionIndex(files, previousFile);
        if (selection < 0 || selection >= (int)m_chapterList->GetCount()) selection = 0;
        m_chapterList->SetSelection(selection);
        ReloadRightList();
        LoadHistory();
    } else {
        SetStatus("No files yet — generate one in the Create tab.");
    }
}

void EditPanel::SaveCurrentFileOrder() const {
    std::string proj = CurrentProjectPath();
    if (proj.empty()) return;

    std::vector<std::string> files;
    for (unsigned int i = 0; i < m_chapterList->GetCount(); ++i)
        files.push_back(m_chapterList->GetString(i).ToStdString());
    SaveFileOrder(proj, files);
}

// Reload the right list based on the active radio button.
void EditPanel::ReloadRightList() {
    if (m_radioChapter->GetValue()) {
        m_rightLabel->SetLabel("Chapters:");
        LoadSections();
    } else {
        m_rightLabel->SetLabel("Tidbits:");
        LoadTidbits();
    }
}

void EditPanel::LoadTidbits() {
    m_tidbitList->Clear();
    m_tidbits.clear();

    std::string path = CurrentChapterPath();
    if (path.empty()) return;

    std::ifstream f(path);
    if (!f) { SetStatus("Cannot read: " + path); return; }
    std::string content((std::istreambuf_iterator<char>(f)), {});

    std::string marker_prefix = "<!-- tb:";
    std::size_t pos = 0;
    while ((pos = content.find(marker_prefix, pos)) != std::string::npos) {
        auto end = content.find(" -->", pos);
        if (end == std::string::npos) break;
        std::string id_str = content.substr(pos + marker_prefix.size(),
                                            end - pos - marker_prefix.size());
        int id = 0;
        try { id = std::stoi(id_str); } catch (...) { pos = end; continue; }

        std::string block = ExtractTidbit(content, id);
        std::string preview = "[empty]";
        if (!block.empty()) {
            std::istringstream ss(block);
            std::string line;
            while (std::getline(ss, line))
                if (!line.empty() && line.substr(0, 3) != ":::") { preview = line; break; }
        }

        m_tidbits.push_back({id, preview});
        m_tidbitList->Append(preview_label("tb", id, preview));
        pos = end;
    }

    SetStatus(m_tidbits.empty() ? "No tidbits found in this file."
                                 : wxString::Format("%d tidbit(s) found.", (int)m_tidbits.size()));
}

void EditPanel::LoadSections() {
    m_tidbitList->Clear();
    m_sections.clear();

    std::string path = CurrentChapterPath();
    if (path.empty()) return;

    std::ifstream f(path);
    if (!f) { SetStatus("Cannot read: " + path); return; }
    std::string content((std::istreambuf_iterator<char>(f)), {});

    std::string marker_prefix = "<!-- ch:";
    std::size_t pos = 0;
    while ((pos = content.find(marker_prefix, pos)) != std::string::npos) {
        auto end = content.find(" -->", pos);
        if (end == std::string::npos) break;
        std::string id_str = content.substr(pos + marker_prefix.size(),
                                            end - pos - marker_prefix.size());
        int id = 0;
        try { id = std::stoi(id_str); } catch (...) { pos = end; continue; }

        std::string block = ExtractChapter(content, id);
        std::string preview = "[empty]";
        if (!block.empty()) {
            std::istringstream ss(block);
            std::string line;
            while (std::getline(ss, line))
                if (line.rfind("## ", 0) == 0) { preview = line.substr(3); break; }
        }

        m_sections.push_back({id, preview});
        m_tidbitList->Append(preview_label("ch", id, preview));
        pos = end;
    }

    SetStatus(m_sections.empty()
              ? "No chapters found — generate with a newer prompt."
              : wxString::Format("%d chapter(s) found.", (int)m_sections.size()));
}

void EditPanel::LoadHistory() {
    m_historyList->Clear();
    m_commits.clear();

    std::string proj = CurrentProjectPath();
    if (proj.empty()) return;

    auto log = GitLogProject(proj);
    for (auto& c : log) {
        m_historyList->Append(wxString::FromUTF8(
            c.date + "  " + c.shortHash + "  " + c.subject));
        m_commits.push_back({c.hash, c.shortHash, c.date, c.subject});
    }

    if (m_commits.empty())
        SetStatus("No commits found for this project.");
}

void EditPanel::OpenCurrentFileInVim() {
    std::string path = CurrentChapterPath();
    if (path.empty()) { SetStatus("Select a file first."); return; }

    std::string vimCmd = "vim " + shell_quote(path);
    std::string itermScript =
        "tell application \"iTerm2\"\n"
        "  activate\n"
        "  create window with default profile\n"
        "  tell current session of current window\n"
        "    write text " + applescript_string(vimCmd) + "\n"
        "  end tell\n"
        "end tell";
    std::string terminalScript =
        "tell application \"Terminal\" to activate\n"
        "tell application \"Terminal\" to do script \"vim \" & quoted form of "
        + applescript_string(path);

    std::string cmd = "osascript -e " + shell_quote(itermScript)
                    + " || osascript -e " + shell_quote(terminalScript);
    long pid = wxExecute(wxString::FromUTF8(cmd), wxEXEC_ASYNC);
    if (pid == 0) {
        SetStatus("Could not open Terminal with Vim.");
        Logger::get().log("Open Vim failed: " + path);
        return;
    }
    SetStatus("Opening in Vim: " + wxString::FromUTF8(fs::path(path).filename().string()));
    Logger::get().log("Open Vim: " + path);
}

// ---------------------------------------------------------------------------

void EditPanel::OnRefresh(wxCommandEvent&)         { RefreshChapters(); }
void EditPanel::OnChapterSelected(wxCommandEvent&) { ReloadRightList(); LoadHistory(); }
void EditPanel::OnChapterActivated(wxCommandEvent&) {
    if (m_radioOpenVim->GetValue()) {
        OpenCurrentFileInVim();
        return;
    }
    std::string path = CurrentChapterPath();
    if (path.empty()) { SetStatus("Select a file first."); return; }
    if (m_openCallback) m_openCallback(path);
    SetStatus("Opened in View tab: " + wxString::FromUTF8(fs::path(path).filename().string()));
}
void EditPanel::OnMoveFileUp(wxCommandEvent&) {
    int sel = m_chapterList->GetSelection();
    if (sel == wxNOT_FOUND || sel == 0) return;
    wxString name = m_chapterList->GetString(sel);
    m_chapterList->Delete((unsigned int)sel);
    m_chapterList->Insert(name, (unsigned int)(sel - 1));
    m_chapterList->SetSelection(sel - 1);
    SaveCurrentFileOrder();
    ReloadRightList();
    LoadHistory();
}
void EditPanel::OnMoveFileDown(wxCommandEvent&) {
    int sel = m_chapterList->GetSelection();
    if (sel == wxNOT_FOUND || sel >= (int)m_chapterList->GetCount() - 1) return;
    wxString name = m_chapterList->GetString(sel);
    m_chapterList->Delete((unsigned int)sel);
    m_chapterList->Insert(name, (unsigned int)(sel + 1));
    m_chapterList->SetSelection(sel + 1);
    SaveCurrentFileOrder();
    ReloadRightList();
    LoadHistory();
}
void EditPanel::OnRenameFile(wxCommandEvent&) {
    std::string oldPath = CurrentChapterPath();
    if (oldPath.empty()) { SetStatus("Select a file first."); return; }

    fs::path oldFs(oldPath);
    wxString oldName = wxString::FromUTF8(oldFs.filename().string());
    wxString newName = wxGetTextFromUser("New filename:", "Rename File", oldName, this).Trim();
    if (newName.empty() || newName == oldName) return;

    fs::path newFs(newName.ToStdString());
    if (newFs.has_parent_path()) {
        SetStatus("Rename failed: enter a filename only, not a path.");
        return;
    }
    if (newFs.extension().empty()) {
        newFs += ".md";
    }
    if (newFs.extension() != ".md") {
        SetStatus("Rename failed: filename must end in .md.");
        return;
    }

    fs::path target = oldFs.parent_path() / newFs;
    if (fs::exists(target)) {
        SetStatus("Rename failed: target file already exists.");
        return;
    }

    std::error_code ec;
    fs::rename(oldFs, target, ec);
    if (ec) {
        SetStatus("Rename failed: " + wxString::FromUTF8(ec.message()));
        Logger::get().log("EditPanel rename FAILED: " + oldPath + " -> " + target.string()
                          + "  " + ec.message());
        return;
    }

    Logger::get().log("EditPanel renamed file: " + oldPath + " -> " + target.string());
    RefreshChapters();
    int idx = m_chapterList->FindString(wxString::FromUTF8(target.filename().string()));
    if (idx != wxNOT_FOUND) {
        m_chapterList->SetSelection(idx);
        ReloadRightList();
        LoadHistory();
        SaveCurrentFileOrder();
    }
    SetStatus("Renamed to: " + wxString::FromUTF8(target.filename().string()));
}
void EditPanel::OnDeleteFile(wxCommandEvent&) {
    std::string path = CurrentChapterPath();
    if (path.empty()) { SetStatus("Select a file first."); return; }

    std::string filename = fs::path(path).filename().string();
    wxString question = "Delete '" + wxString::FromUTF8(filename) + "'?\n\n"
                        "This removes the file from disk. Git history may still contain older committed versions.";
    if (wxMessageBox(question, "Confirm delete", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES)
        return;

    std::error_code ec;
    bool removed = fs::remove(path, ec);
    if (!removed || ec) {
        SetStatus("Delete failed: " + wxString::FromUTF8(ec ? ec.message() : "file was not removed"));
        Logger::get().log("EditPanel delete FAILED: " + path + "  " + (ec ? ec.message() : "not removed"));
        return;
    }

    Logger::get().log("EditPanel deleted file: " + path);
    RefreshChapters();
    SaveCurrentFileOrder();
    SetStatus("Deleted: " + wxString::FromUTF8(filename));
}
void EditPanel::OnTargetChanged(wxCommandEvent&)   { ReloadRightList(); }

void EditPanel::SetStatus(const wxString& msg) { m_statusCtrl->SetValue(msg); }
void EditPanel::SetBusy(bool on) {
    m_rewriteBtn->Enable(!on);
    m_rewriteBtn->SetLabel(on ? "Rewriting…" : "Rewrite");
    m_translateBtn->Enable(!on);
    m_commitBtn->Enable(!on);
    m_moveUpBtn->Enable(!on);
    m_moveDownBtn->Enable(!on);
    m_renameFileBtn->Enable(!on);
    m_deleteFileBtn->Enable(!on);
    m_checkoutBtn->Enable(!on);
    m_stashBtn->Enable(!on);
    m_unstashBtn->Enable(!on);
}

// ── Rewrite via LLM ──────────────────────────────────────────────────────────

void EditPanel::OnRewrite(wxCommandEvent&) {
    wxString instr = m_instructCtrl->GetValue().Trim();
    if (instr.empty()) { SetStatus("Enter an instruction first."); return; }

    std::string chapterPath = CurrentChapterPath();
    if (chapterPath.empty()) { SetStatus("Select a file first."); return; }

    std::string chapterContent;
    {
        std::ifstream f(chapterPath);
        if (!f) { SetStatus("Cannot read file."); return; }
        chapterContent.assign(std::istreambuf_iterator<char>(f), {});
    }

    bool chapterMode = m_radioChapter->GetValue();
    int tidbitId  = -1;
    int sectionId = -1;
    std::string originalBlock;

    if (chapterMode) {
        int sel = m_tidbitList->GetSelection();
        if (sel == wxNOT_FOUND || sel >= (int)m_sections.size()) {
            SetStatus("Select a chapter from the list."); return;
        }
        sectionId     = m_sections[sel].id;
        originalBlock = ExtractChapter(chapterContent, sectionId);
        if (originalBlock.empty()) {
            SetStatus("Could not extract chapter — try Refresh."); return;
        }
    } else {
        int sel = m_tidbitList->GetSelection();
        if (sel == wxNOT_FOUND || sel >= (int)m_tidbits.size()) {
            SetStatus("Select a tidbit from the list."); return;
        }
        tidbitId      = m_tidbits[sel].id;
        originalBlock = ExtractTidbit(chapterContent, tidbitId);
        if (originalBlock.empty()) {
            SetStatus("Could not extract tidbit — try Refresh."); return;
        }
    }

    AppState st = LoadAppState();
    LLMConfig cfg = llm_config_from_state(st);

    // Clipboard mode: copy the prompt so the user can paste it into any LLM manually.
    if (cfg.backend == LLMBackend::Clipboard) {
        std::string prompt = BuildPatchPrompt(originalBlock, instr.ToStdString(), "", chapterContent);
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(prompt)));
            wxTheClipboard->Close();
        }
        SetStatus("Prompt copied to clipboard.\n\n"
                  "Paste into any LLM, copy the rewritten block, then paste it back\n"
                  "into the file manually — or set a direct backend in the Create tab\n"
                  "to have the rewrite applied automatically.");
        return;
    }

    std::string backendLabel = st.backend;
    std::string prompt   = BuildPatchPrompt(originalBlock, instr.ToStdString(), "", chapterContent);
    std::string chapPath = chapterPath;
    OpenCallback cb      = m_openCallback;

    Logger::get().log("EditPanel rewrite: file=" + chapPath
                      + "  mode=" + (chapterMode ? "chapter" : "tidbit")
                      + "  id=" + std::to_string(chapterMode ? sectionId : tidbitId)
                      + "  backend=" + st.backend);

    SetBusy(true);
    SetStatus("Sending to " + wxString::FromUTF8(backendLabel) + "…");

    std::thread([this, prompt, cfg, chapPath, chapterMode, tidbitId, sectionId, cb]() mutable {
        auto started = std::chrono::steady_clock::now();
        LLMResult res = InvokeLLM(prompt, cfg);
        int durationSeconds = (int)std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started).count();
        if (res.ok) {
            std::string topic = (chapterMode ? "chapter " : "tidbit ")
                              + std::to_string(chapterMode ? sectionId : tidbitId);
            RecordLLMTiming(fs::path(chapPath).parent_path().string(),
                            "patch", topic, durationSeconds);
        }

        wxTheApp->CallAfter([this, res, chapPath, chapterMode, tidbitId, sectionId, cb]() mutable {
            SetBusy(false);
            if (!res.ok) {
                Logger::get().log("EditPanel rewrite FAILED: " + res.error);
                SetStatus("Error: " + wxString::FromUTF8(res.error));
                return;
            }

            bool ok = chapterMode
                      ? ApplyChapterPatch(chapPath, sectionId, res.text)
                      : ApplyTidbitPatch(chapPath, tidbitId, res.text);

            if (!ok) { SetStatus("Rewrite failed — could not write file."); return; }

            ReloadRightList();
            LoadHistory();
            SetStatus("Done. Rendering updated file.");
            if (cb) cb(chapPath);
        });
    }).detach();
}

void EditPanel::OnTranslate(wxCommandEvent&) {
    wxString lang = m_translateLangCtrl->GetValue().Trim();
    if (lang.empty()) { SetStatus("Enter a target language first."); return; }

    std::string sourcePath = CurrentChapterPath();
    if (sourcePath.empty()) { SetStatus("Select a file first."); return; }

    std::string sourceContent;
    wxArrayInt selectedVersions;
    m_historyList->GetSelections(selectedVersions);
    if (selectedVersions.size() == 1) {
        int idx = selectedVersions[0];
        if (idx < 0 || idx >= (int)m_commits.size()) return;
        std::string proj = CurrentProjectPath();
        std::string relPath = fs::path(sourcePath).filename().string();
        sourceContent = GitShowFile(proj, m_commits[idx].hash, relPath);
        if (sourceContent.empty()) { SetStatus("Could not read selected version."); return; }
    } else {
        std::ifstream f(sourcePath);
        if (!f) { SetStatus("Cannot read selected file."); return; }
        sourceContent.assign(std::istreambuf_iterator<char>(f), {});
    }

    AppState st = LoadAppState();
    LLMConfig cfg = llm_config_from_state(st);
    wxString extraInstr = m_instructCtrl->GetValue().Trim();
    std::string prompt = BuildTranslationPrompt(sourceContent,
                                                lang.ToStdString(),
                                                "",
                                                extraInstr.ToStdString());

    if (cfg.backend == LLMBackend::Clipboard) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(prompt)));
            wxTheClipboard->Close();
        }
        SetStatus("Translation prompt copied to clipboard.\n\n"
                  "Paste it into any LLM, then save the translated markdown as a new file.");
        return;
    }

    fs::path src(sourcePath);
    std::string suffix = language_suffix(lang.ToStdString());
    fs::path outPath = src.parent_path() / (src.stem().string() + "_" + suffix + src.extension().string());
    OpenCallback cb = m_openCallback;
    std::string backendLabel = st.backend.empty() ? "LLM" : st.backend;

    Logger::get().log("EditPanel translate: file=" + sourcePath
                      + "  target=" + lang.ToStdString()
                      + "  backend=" + backendLabel);

    SetBusy(true);
    SetStatus("Translating to " + lang + " with " + wxString::FromUTF8(backendLabel) + "…");

    std::thread([this, prompt, cfg, outPath, cb]() mutable {
        auto started = std::chrono::steady_clock::now();
        LLMResult res = InvokeLLM(prompt, cfg);
        int durationSeconds = (int)std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started).count();
        if (res.ok) {
            RecordLLMTiming(outPath.parent_path().string(),
                            "translate", outPath.filename().string(), durationSeconds);
        }
        wxTheApp->CallAfter([this, res, outPath, cb]() mutable {
            SetBusy(false);
            if (!res.ok) {
                Logger::get().log("EditPanel translate FAILED: " + res.error);
                SetStatus("Error: " + wxString::FromUTF8(res.error));
                return;
            }
            {
                std::ofstream f(outPath);
                f << CleanMarkdownResponse(res.text);
                if (!f.good()) {
                    SetStatus("Translation failed — could not write file.");
                    return;
                }
            }
            RefreshChapters();
            SetStatus("Translated file saved: " + wxString::FromUTF8(outPath.filename().string()));
            if (cb) cb(outPath.string());
        });
    }).detach();
}

// ── Git operations ────────────────────────────────────────────────────────────

void EditPanel::OnCommit(wxCommandEvent&) {
    std::string proj = CurrentProjectPath();
    std::string path = CurrentChapterPath();
    if (proj.empty() || path.empty()) { SetStatus("Select a file first."); return; }

    wxString msg = m_commitMsgCtrl->GetValue().Trim();
    if (msg.empty()) { SetStatus("Enter a commit message first."); return; }

    std::string relPath = fs::path(path).filename().string();
    bool ok = GitCommitFile(proj, relPath, msg.ToStdString());
    if (!ok) {
        SetStatus("Commit failed — is git configured for this project?");
        return;
    }
    m_commitMsgCtrl->Clear();
    LoadHistory();
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        wxTheApp->CallAfter([this]() { LoadHistory(); });
    }).detach();
    SetStatus("Committed: " + msg);
    Logger::get().log("EditPanel committed: " + relPath + " — " + msg.ToStdString());
}

void EditPanel::OnViewVersion(wxCommandEvent&) {
    wxArrayInt sel;
    m_historyList->GetSelections(sel);
    if (sel.empty()) { SetStatus("Select one commit from history to view."); return; }

    int idx = sel[0];
    if (idx < 0 || idx >= (int)m_commits.size()) return;

    std::string proj    = CurrentProjectPath();
    std::string path    = CurrentChapterPath();
    if (proj.empty() || path.empty()) return;

    std::string relPath = fs::path(path).filename().string();
    std::string content = GitShowFile(proj, m_commits[idx].hash, relPath);
    if (content.empty()) { SetStatus("Could not retrieve that version."); return; }

    // Write to a temp file so the View tab can render it.
    fs::path tmp = fs::temp_directory_path() /
                   ("storyteller_ver_" + m_commits[idx].shortHash + "_" + relPath);
    {
        std::ofstream f(tmp);
        f << content;
    }
    if (m_openCallback) m_openCallback(tmp.string());
    SetStatus("Viewing " + m_commits[idx].shortHash + ": " + m_commits[idx].subject);
}

void EditPanel::OnDiff(wxCommandEvent&) {
    wxArrayInt sel;
    m_historyList->GetSelections(sel);
    if (sel.size() > 2) {
        SetStatus("Select 0, 1, or 2 commits to diff.");
        return;
    }

    std::string proj = CurrentProjectPath();
    std::string path = CurrentChapterPath();
    if (proj.empty() || path.empty()) { SetStatus("Select a file first."); return; }
    std::string relPath = fs::path(path).filename().string();

    std::string hash1, hash2;
    if (sel.empty()) {
        // Nothing selected: diff HEAD vs working copy (show uncommitted changes).
        hash1 = "HEAD";
        hash2 = "";
    } else if (sel.size() == 1) {
        int idx = sel[0];
        if (idx < 0 || idx >= (int)m_commits.size()) return;
        hash1 = m_commits[idx].hash;
        hash2 = "";  // diff vs working copy
    } else {
        // Ensure older commit is hash1 (higher index = older in log)
        int a = sel[0], b = sel[1];
        if (a > b) std::swap(a, b);
        if (a < 0 || b >= (int)m_commits.size()) return;
        hash1 = m_commits[b].hash;  // older
        hash2 = m_commits[a].hash;  // newer
    }

    std::string html = GitDiffHTML(proj, hash1, hash2, relPath);

    fs::path tmp = fs::temp_directory_path() / ("storyteller_diff_" + relPath + ".html");
    {
        std::ofstream f(tmp);
        f << html;
    }
    if (m_openCallback) m_openCallback(tmp.string());
    if (sel.empty()) {
        SetStatus("Diff opened: HEAD vs current file.");
    } else if (sel.size() == 1) {
        int idx = sel[0];
        SetStatus("Diff opened: " + m_commits[idx].shortHash + " vs current file.");
    } else {
        SetStatus("Diff opened between selected versions.");
    }
}

void EditPanel::OnRestore(wxCommandEvent&) {
    wxArrayInt sel;
    m_historyList->GetSelections(sel);
    if (sel.size() != 1) { SetStatus("Select exactly one commit to restore."); return; }

    int idx = sel[0];
    if (idx < 0 || idx >= (int)m_commits.size()) return;

    std::string proj    = CurrentProjectPath();
    std::string path    = CurrentChapterPath();
    if (proj.empty() || path.empty()) return;
    std::string relPath = fs::path(path).filename().string();

    wxString question = wxString::Format(
        "Restore '%s' to version %s (%s)?\nThis will overwrite the working copy.",
        relPath, m_commits[idx].shortHash, m_commits[idx].subject);
    if (wxMessageBox(question, "Confirm restore", wxYES_NO | wxICON_WARNING, this) != wxYES)
        return;

    bool ok = GitRestoreFile(proj, m_commits[idx].hash, relPath);
    if (!ok) { SetStatus("Restore failed."); return; }

    RefreshChapters();
    if (m_openCallback) m_openCallback(path);
    SetStatus("Restored to " + m_commits[idx].shortHash + ": " + m_commits[idx].subject);
    Logger::get().log("EditPanel restored: " + relPath + " to " + m_commits[idx].hash);
}

void EditPanel::OnCheckout(wxCommandEvent&) {
    wxArrayInt sel;
    m_historyList->GetSelections(sel);
    if (sel.size() != 1) { SetStatus("Select exactly one commit to checkout."); return; }

    int idx = sel[0];
    if (idx < 0 || idx >= (int)m_commits.size()) return;

    std::string proj = CurrentProjectPath();
    if (proj.empty()) return;

    wxString question = wxString::Format(
        "Checkout the whole project folder to version %s (%s)?\n\n"
        "This affects every file in this story folder and may leave git in detached HEAD.\n"
        "Use Stash first if you have local changes you want to keep.",
        m_commits[idx].shortHash, m_commits[idx].subject);
    if (wxMessageBox(question, "Confirm checkout", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES)
        return;

    std::string hash = m_commits[idx].hash;
    std::string shortHash = m_commits[idx].shortHash;
    std::string subject = m_commits[idx].subject;
    bool ok = GitCheckoutCommit(proj, hash);
    if (!ok) {
        SetStatus("Checkout failed. Stash or commit local changes, then try again.");
        return;
    }

    RefreshChapters();
    std::string path = CurrentChapterPath();
    if (!path.empty() && m_openCallback) m_openCallback(path);
    SetStatus("Checked out project folder at " + shortHash + ": " + subject);
    Logger::get().log("EditPanel checkout: " + proj + " to " + hash);
}

void EditPanel::OnStash(wxCommandEvent&) {
    std::string proj = CurrentProjectPath();
    if (proj.empty()) return;

    bool ok = GitStashProject(proj, "StoryTeller stash");
    RefreshChapters();
    SetStatus(ok ? "Stashed local project-folder changes."
                 : "Stash failed or there were no changes to stash.");
    Logger::get().log(std::string("EditPanel stash ") + (ok ? "OK: " : "FAILED: ") + proj);
}

void EditPanel::OnUnstash(wxCommandEvent&) {
    std::string proj = CurrentProjectPath();
    if (proj.empty()) return;

    bool ok = GitUnstashProject(proj);
    RefreshChapters();
    std::string path = CurrentChapterPath();
    if (!path.empty() && m_openCallback) m_openCallback(path);
    SetStatus(ok ? "Unstashed latest project-folder changes."
                 : "Unstash failed. There may be no stash, or there may be conflicts.");
    Logger::get().log(std::string("EditPanel unstash ") + (ok ? "OK: " : "FAILED: ") + proj);
}
