#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <wx/wx.h>
#include <wx/webview.h>
#include <wx/splitter.h>
#include "session.h"
#include "exam_prompt.h"
#include "turn_chat_panel.h"
#include "config.h"
#include "llm.h"

class ExamPanel : public wxPanel {
public:
    using SessionCompleteCallback = std::function<void(const std::string& sessionFile)>;
    using DeepDiveCallback        = std::function<void()>;
    using SavedConvoCallback      = std::function<void()>;

    ExamPanel(wxWindow* parent,
              SessionCompleteCallback onSessionComplete,
              DeepDiveCallback        onDeepDive     = {},
              SavedConvoCallback      onSavedConvo   = {});

    void StartSession(const std::string& projectDir,
                      const std::string& sessionFile,
                      const ExamConfig&  cfg,
                      const LLMConfig&   llmCfg,
                      bool               darkMode);

    void ResumeSession(const std::string& projectDir,
                       const std::string& sessionFile,
                       const LLMConfig&   llmCfg,
                       bool               darkMode);

    void StartDrill(const std::string& projectDir,
                    const std::string& sessionFile,
                    int                questionIndex,
                    const ExamConfig&  cfg,
                    const LLMConfig&   llmCfg,
                    bool               darkMode);

    bool HasActiveSession() const { return m_active; }
    void SetDarkMode(bool dark);
    void AbandonSession();
    void Clear();

private:
    SessionCompleteCallback   m_onComplete;
    DeepDiveCallback          m_onDeepDive;
    SavedConvoCallback        m_onSavedConvo;

    bool              m_active        = false;
    bool              m_busy          = false;
    bool              m_chatOpen      = false;
    std::string       m_projectDir;
    std::string       m_sessionFile;
    ExamConfig        m_cfg;
    LLMConfig         m_llmCfg;
    bool              m_darkMode      = false;
    int               m_questionIndex = 0;
    std::string       m_currentQuestion;
    std::string       m_hintText;
    std::string       m_statusText;
    std::vector<QuestionTurn>  m_turns;
    std::vector<HistoryGroup>  m_historyGroups;

    wxSplitterWindow* m_splitter   = nullptr;
    TurnChatPanel*    m_chatPanel  = nullptr;
    wxWebView*        m_webView    = nullptr;

    void RequestFirstQuestion();
    void RequestNextQuestion();
    void SubmitAnswer(const std::string& answer);
    void Render(bool scrollToBottom = false);
    std::string BuildExamHTML(bool scrollToBottom = false) const;

    void OnExamAction(wxWebViewEvent&);
    void OnWebViewNav(wxWebViewEvent&);

    wxDECLARE_EVENT_TABLE();
};
