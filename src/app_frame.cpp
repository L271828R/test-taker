#include "app_frame.h"
#include "new_session_panel.h"
#include "exam_panel.h"
#include "review_panel.h"
#include "chat_panel.h"
#include "exam_meta.h"
#include "config.h"
#include "logger.h"
#include "html_template.h"
#include <wx/config.h>
#include <fstream>

// Notebook tab indices
enum TabIndex { TAB_PROJECTS = 0, TAB_NEW_SESSION, TAB_EXAM, TAB_REVIEW, TAB_CHAT };

wxBEGIN_EVENT_TABLE(AppFrame, wxFrame)
    EVT_MENU(ID_THEME_LIGHT,  AppFrame::OnThemeLight)
    EVT_MENU(ID_THEME_DARK,   AppFrame::OnThemeDark)
    EVT_MENU(ID_VIEW_LOGS,    AppFrame::OnViewLogs)
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
    menuView->Append(ID_VIEW_LOGS, "View Logs");

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
        [this](const std::string& sessionFile){ OnSessionComplete(sessionFile); });

    // Review tab
    m_reviewPage = new ReviewPanel(m_notebook,
        [this](const std::string& projDir, const std::string& sessionFile, int idx){
            OnDrillRequested(projDir, sessionFile, idx);
        });

    // Chat tab
    m_chatPage = new ChatPanel(m_notebook, m_darkMode);

    m_notebook->AddPage(m_projectPage,    "Projects");
    m_notebook->AddPage(m_newSessionPage, "New Session");
    m_notebook->AddPage(m_examPage,       "Exam");
    m_notebook->AddPage(m_reviewPage,     "Review");
    m_notebook->AddPage(m_chatPage,       "Chat");

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

    // Resume last incomplete session if one exists for this project.
    if (!state.lastSessionFile.empty() && !m_examPage->HasActiveSession()) {
        std::string sessionPath = projectDir + "/" + state.lastSessionFile;
        std::ifstream check(sessionPath);
        if (check.good()) {
            m_examPage->ResumeSession(projectDir, sessionPath, llmCfg, m_darkMode);
            m_notebook->SetSelection(TAB_EXAM);
            Logger::get().log("Auto-resumed session: " + sessionPath);
        }
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

void AppFrame::OnExit(wxCommandEvent&) { Close(); }

void AppFrame::OnClose(wxCloseEvent& evt) {
    Logger::get().log("=== AppFrame closing ===");
    evt.Skip();
}
