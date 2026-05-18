#pragma once
#include <wx/wx.h>
#include <wx/notebook.h>
#include <functional>
#include <string>
#include "project_panel.h"
#include "exam_prompt.h"
#include "llm.h"

// Forward declarations — panels are included in app_frame.cpp only.
class NewSessionPanel;
class ExamPanel;
class ReviewPanel;
class ChatPanel;
class CorpusPanel;
class SavedPanel;

enum {
    ID_THEME_LIGHT  = wxID_HIGHEST + 1,
    ID_THEME_DARK,
    ID_VIEW_LOGS,
    ID_VIEW_RAG_LOGS,
    ID_FONT_INCREASE,
    ID_FONT_DECREASE,
    ID_FONT_RESET,
};

class AppFrame : public wxFrame {
public:
    AppFrame();

private:
    wxNotebook*       m_notebook        = nullptr;
    ProjectPanel*     m_projectPage     = nullptr;
    NewSessionPanel*  m_newSessionPage  = nullptr;
    ExamPanel*        m_examPage        = nullptr;
    ReviewPanel*      m_reviewPage      = nullptr;
    ChatPanel*        m_chatPage        = nullptr;
    CorpusPanel*      m_corpusPage      = nullptr;
    SavedPanel*       m_savedPage       = nullptr;

    bool m_darkMode        = false;
    int  m_fontSizePercent = 100;
    std::string m_activeProjectDir;

    // ── Callbacks wired between panels ───────────────────────────────────
    void OnProjectActivated(const std::string& projectDir);
    void OnSessionStarted(const std::string& projectDir,
                          const std::string& sessionFile,
                          const ExamConfig&  cfg,
                          const LLMConfig&   llmCfg);
    void OnSessionComplete(const std::string& sessionFile);
    void OnDrillRequested(const std::string& projectDir,
                          const std::string& sessionFile,
                          int                questionIndex);
    void OnDeepDiveRequested();

    // ── Menu handlers ─────────────────────────────────────────────────────
    void OnThemeLight(wxCommandEvent&);
    void OnThemeDark(wxCommandEvent&);
    void OnViewLogs(wxCommandEvent&);
    void OnViewRagLogs(wxCommandEvent&);
    void OnFontIncrease(wxCommandEvent&);
    void OnFontDecrease(wxCommandEvent&);
    void OnFontReset(wxCommandEvent&);
    void OnExit(wxCommandEvent&);
    void OnClose(wxCloseEvent&);

    wxDECLARE_EVENT_TABLE();
};
