#include "app_frame.h"
#include "new_session_panel.h"
#include "exam_panel.h"
#include "review_panel.h"
#include "chat_panel.h"
#include "corpus_panel.h"
#include "corpus.h"
#include "exam_meta.h"
#include "focus_list_panel.h"
#include "config.h"
#include "logger.h"
#include "html_template.h"
#include <wx/config.h>
#include <wx/spinctrl.h>
#include <fstream>

// Notebook tab indices
enum TabIndex { TAB_PROJECTS = 0, TAB_NEW_SESSION, TAB_EXAM, TAB_REVIEW, TAB_CHAT, TAB_CORPUS };

wxBEGIN_EVENT_TABLE(AppFrame, wxFrame)
    EVT_MENU(ID_THEME_LIGHT,  AppFrame::OnThemeLight)
    EVT_MENU(ID_THEME_DARK,   AppFrame::OnThemeDark)
    EVT_MENU(ID_VIEW_LOGS,     AppFrame::OnViewLogs)
    EVT_MENU(ID_VIEW_RAG_LOGS, AppFrame::OnViewRagLogs)
    EVT_MENU(ID_FONT_INCREASE, AppFrame::OnFontIncrease)
    EVT_MENU(ID_FONT_DECREASE, AppFrame::OnFontDecrease)
    EVT_MENU(ID_FONT_RESET,    AppFrame::OnFontReset)
    EVT_MENU(wxID_EXIT,        AppFrame::OnExit)
    EVT_CLOSE(AppFrame::OnClose)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
AppFrame::AppFrame()
    : wxFrame(nullptr, wxID_ANY, "TestTaker",
              wxDefaultPosition, wxSize(1100, 780))
{
    // ── Persist dark-mode preference ─────────────────────────────────────
    wxConfig cfg("TestTaker");
    cfg.Read("darkMode",        &m_darkMode,        false);
    cfg.Read("fontSizePercent", &m_fontSizePercent, 100);

    // ── Menu bar ─────────────────────────────────────────────────────────
    auto* menuFile = new wxMenu;
    menuFile->Append(wxID_EXIT, "Quit\tCtrl+Q");

    auto* menuView = new wxMenu;
    menuView->AppendRadioItem(ID_THEME_LIGHT, "Light Mode\tCtrl+Shift+L");
    menuView->AppendRadioItem(ID_THEME_DARK,  "Dark Mode\tCtrl+Shift+D");
    menuView->Check(m_darkMode ? ID_THEME_DARK : ID_THEME_LIGHT, true);
    menuView->AppendSeparator();
    menuView->Append(ID_FONT_INCREASE, "Increase Font\tCtrl++");
    menuView->Append(ID_FONT_DECREASE, "Decrease Font\tCtrl+-");
    menuView->Append(ID_FONT_RESET,    "Reset Font\tCtrl+0");
    menuView->AppendSeparator();
    menuView->Append(ID_VIEW_LOGS,     "View Logs");
    menuView->Append(ID_VIEW_RAG_LOGS, "View RAG Logs");

    auto* menuBar = new wxMenuBar;
    menuBar->Append(menuFile, "&File");
    menuBar->Append(menuView, "&View");
    SetMenuBar(menuBar);
    CreateStatusBar();

    // ── Notebook with five tabs ───────────────────────────────────────────
    m_notebook = new wxNotebook(this, wxID_ANY);

    // Projects tab
    m_projectPage = new ProjectPanel(m_notebook,
        [this](const std::string& dir){ OnProjectActivated(dir); });

    // New Session tab
    m_newSessionPage = new NewSessionPanel(m_notebook,
        [this](const std::string& projDir, const std::string& sessionFile,
               const ExamConfig& cfg, const LLMConfig& llmCfg){
            OnSessionStarted(projDir, sessionFile, cfg, llmCfg);
        });

    // Exam tab
    m_examPage = new ExamPanel(m_notebook,
        [this](const std::string& sessionFile){ OnSessionComplete(sessionFile); },
        [this](){ OnDeepDiveRequested(); });

    // Review tab
    m_reviewPage = new ReviewPanel(m_notebook,
        [this](const std::string& projDir, const std::string& sessionFile, int idx){
            OnDrillRequested(projDir, sessionFile, idx);
        });

    // Chat tab
    m_chatPage = new ChatPanel(m_notebook, m_darkMode);

    // Corpus tab
    m_corpusPage = new CorpusPanel(m_notebook);

    m_notebook->AddPage(m_projectPage,    "Projects");
    m_notebook->AddPage(m_newSessionPage, "New Session");
    m_notebook->AddPage(m_examPage,       "Exam");
    m_notebook->AddPage(m_reviewPage,     "Review");
    m_notebook->AddPage(m_chatPage,       "Chat");
    m_notebook->AddPage(m_corpusPage,     "Corpus");

    auto* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(m_notebook, 1, wxEXPAND);
    SetSizer(frameSizer);

    Logger::get().log("=== AppFrame ready ===");
}

// ---------------------------------------------------------------------------
void AppFrame::OnProjectActivated(const std::string& projectDir) {
    m_activeProjectDir = projectDir;
    RecordExamOpen(projectDir);

    // Propagate to tabs that need the active project
    m_newSessionPage->SyncProject(projectDir);
    m_reviewPage->RefreshSessions(projectDir);

    // Build LLM config from persisted app state so ChatPanel has credentials
    AppState state = LoadAppState();
    LLMConfig llmCfg;
    llmCfg.backend = LLMBackend::ClaudeP; // default; NewSessionPanel overrides per session
    if (state.backend == "Anthropic API") {
        llmCfg.backend = LLMBackend::API;
        llmCfg.apiKey  = state.apiKey;
    } else if (state.backend == "Ollama") {
        llmCfg.backend     = LLMBackend::Ollama;
        llmCfg.ollamaModel = state.ollamaModel;
    }
    m_chatPage->SyncProject(projectDir, llmCfg, m_darkMode);
    m_corpusPage->SyncProject(projectDir, llmCfg.ollamaUrl);

    // Always reset the exam panel when switching projects. Any in-progress turns
    // are already persisted via AppendSessionTurn, so no data is lost.
    {
        bool resumed = false;
        if (!state.lastSessionFile.empty()) {
            std::string sessionPath = projectDir + "/" + state.lastSessionFile;
            std::ifstream check(sessionPath);
            if (check.good()) {
                m_examPage->ResumeSession(projectDir, sessionPath, llmCfg, m_darkMode);
                Logger::get().log("Auto-resumed session: " + sessionPath);
                resumed = true;
            }
        }
        if (!resumed) m_examPage->Clear();
    }

    SetStatusText("Project: " + projectDir);
    Logger::get().log("Project activated: " + projectDir);
}

// ---------------------------------------------------------------------------
void AppFrame::OnSessionStarted(const std::string& projectDir,
                                 const std::string& sessionFile,
                                 const ExamConfig&  cfg,
                                 const LLMConfig&   llmCfg) {
    m_activeProjectDir = projectDir;

    m_examPage->StartSession(projectDir, sessionFile, cfg, llmCfg, m_darkMode);
    m_notebook->SetSelection(TAB_EXAM);
}

// ---------------------------------------------------------------------------
void AppFrame::OnSessionComplete(const std::string& sessionFile) {
    // ReviewPanel will pick up the new session when refreshed
    m_reviewPage->RefreshSessions(m_activeProjectDir);
    SetStatusText("Session complete: " + sessionFile);
    Logger::get().log("Session complete: " + sessionFile);
}

// ---------------------------------------------------------------------------
void AppFrame::OnDrillRequested(const std::string& projectDir,
                                 const std::string& sessionFile,
                                 int                questionIndex) {
    AppState state = LoadAppState();
    LLMConfig llmCfg;
    llmCfg.backend = LLMBackend::ClaudeP;
    if (state.backend == "Anthropic API") {
        llmCfg.backend = LLMBackend::API;
        llmCfg.apiKey  = state.apiKey;
    } else if (state.backend == "Ollama") {
        llmCfg.backend     = LLMBackend::Ollama;
        llmCfg.ollamaModel = state.ollamaModel;
    }

    ExamConfig cfg;
    cfg.topic          = "Review drill";
    cfg.difficulty     = "mixed";
    cfg.totalQuestions = 1;

    // Load context.md if present
    std::ifstream ctxFile(projectDir + "/context.md");
    if (ctxFile) cfg.projectContext.assign(
        std::istreambuf_iterator<char>(ctxFile), {});

    m_examPage->StartDrill(projectDir, sessionFile, questionIndex,
                           cfg, llmCfg, m_darkMode);
    m_notebook->SetSelection(TAB_EXAM);
}

// ---------------------------------------------------------------------------
void AppFrame::OnThemeLight(wxCommandEvent&) {
    if (!m_darkMode) return;
    m_darkMode = false;
    wxConfig("TestTaker").Write("darkMode", false);
    m_examPage->SetDarkMode(false);
    m_chatPage->SetDarkMode(false);
}

void AppFrame::OnThemeDark(wxCommandEvent&) {
    if (m_darkMode) return;
    m_darkMode = true;
    wxConfig("TestTaker").Write("darkMode", true);
    m_examPage->SetDarkMode(true);
    m_chatPage->SetDarkMode(true);
}

void AppFrame::OnViewLogs(wxCommandEvent&) {
    std::string logPath = std::string(getenv("HOME") ? getenv("HOME") : "")
                        + "/Library/Logs/TestTaker/test-taker.log";
    std::ifstream f(logPath);
    std::string raw;
    if (f) raw.assign(std::istreambuf_iterator<char>(f), {});
    std::string html = BuildLogsHTML(raw, logPath, m_darkMode);

    // Open a simple viewer window
    auto* win = new wxFrame(this, wxID_ANY, "Logs", wxDefaultPosition, wxSize(900, 600));
    auto* wv  = wxWebView::New(win, wxID_ANY, "about:blank");
    wv->SetPage(wxString::FromUTF8(html), "");
    win->Show();
}

void AppFrame::OnViewRagLogs(wxCommandEvent&) {
    std::string logPath = std::string(getenv("HOME") ? getenv("HOME") : "")
                        + "/Library/Logs/TestTaker/rag.log";
    std::ifstream f(logPath);
    std::string raw;
    if (f) raw.assign(std::istreambuf_iterator<char>(f), {});
    std::string html = BuildRagLogsHTML(raw, logPath, m_darkMode);

    auto* win = new wxFrame(this, wxID_ANY, "RAG Logs", wxDefaultPosition, wxSize(960, 700));
    auto* wv  = wxWebView::New(win, wxID_ANY, "about:blank");
    wv->SetPage(wxString::FromUTF8(html), "");
    win->Show();
}

void AppFrame::OnFontIncrease(wxCommandEvent&) {
    m_fontSizePercent = std::min(200, m_fontSizePercent + 10);
    wxConfig("TestTaker").Write("fontSizePercent", m_fontSizePercent);
}

void AppFrame::OnFontDecrease(wxCommandEvent&) {
    m_fontSizePercent = std::max(50, m_fontSizePercent - 10);
    wxConfig("TestTaker").Write("fontSizePercent", m_fontSizePercent);
}

void AppFrame::OnFontReset(wxCommandEvent&) {
    m_fontSizePercent = 100;
    wxConfig("TestTaker").Write("fontSizePercent", 100);
}

// ---------------------------------------------------------------------------
void AppFrame::OnDeepDiveRequested() {
    // ── Build dialog: focus-area list + difficulty + question count ───────
    wxDialog dlg(this, wxID_ANY, "Focus Areas for Next Session",
                 wxDefaultPosition, wxSize(520, 400));

    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* inner = new wxBoxSizer(wxVERTICAL);

    inner->Add(new wxStaticText(&dlg, wxID_ANY,
        "Add sub-topics you want drilled. Rate priority with stars.\n"
        "Each question randomly picks one area — higher stars = more likely."),
        0, wxBOTTOM, 8);

    auto* focusPanel = new FocusListPanel(&dlg, 180);
    inner->Add(focusPanel, 0, wxEXPAND | wxBOTTOM, 12);

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(&dlg, wxID_ANY, "Difficulty:"),
             0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    wxArrayString diffs;
    for (auto* d : {"mixed", "easy", "medium", "hard"}) diffs.Add(d);
    auto* diffCtrl = new wxChoice(&dlg, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, diffs);
    diffCtrl->SetSelection(0);
    row->Add(diffCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);

    row->Add(new wxStaticText(&dlg, wxID_ANY, "Questions:"),
             0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* countCtrl = new wxSpinCtrl(&dlg, wxID_ANY, "10",
        wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, 50, 10);
    row->Add(countCtrl, 0, wxALIGN_CENTER_VERTICAL);
    inner->Add(row, 0, wxBOTTOM, 12);

    auto* btns = dlg.CreateButtonSizer(wxOK | wxCANCEL);
    inner->Add(btns, 0, wxEXPAND);

    outer->Add(inner, 1, wxEXPAND | wxALL, 14);
    dlg.SetSizer(outer);
    dlg.Layout();

    if (dlg.ShowModal() != wxID_OK) return;

    auto focusAreas = focusPanel->GetAreas();
    std::string difficulty = diffCtrl->GetString(diffCtrl->GetSelection()).ToStdString();
    int count = countCtrl->GetValue();

    m_newSessionPage->PreFill("", focusAreas, difficulty, count);
    m_notebook->SetSelection(TAB_NEW_SESSION);
}

void AppFrame::OnExit(wxCommandEvent&) { Close(); }

void AppFrame::OnClose(wxCloseEvent& evt) {
    Logger::get().log("=== AppFrame closing ===");
    m_newSessionPage->SaveFormState();
    evt.Skip();
}
