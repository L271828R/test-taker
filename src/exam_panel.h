#pragma once
#include <functional>
#include <string>
#include <vector>
#include <wx/wx.h>
#include <wx/webview.h>
#include "session.h"
#include "exam_prompt.h"
#include "llm.h"

class ExamPanel : public wxPanel {
public:
    using SessionCompleteCallback = std::function<void(const std::string& sessionFile)>;

    ExamPanel(wxWindow* parent, SessionCompleteCallback onSessionComplete);

    void StartSession(const std::string& projectDir,
                      const std::string& sessionFile,
                      const ExamConfig&  cfg,
                      const LLMConfig&   llmCfg,
                      bool               darkMode);

    // Re-drill a single flagged question.
    void StartDrill(const std::string& projectDir,
                    const std::string& sessionFile,
                    int                questionIndex,
                    const ExamConfig&  cfg,
                    const LLMConfig&   llmCfg,
                    bool               darkMode);

    bool HasActiveSession() const { return m_active; }

private:
    SessionCompleteCallback   m_onComplete;

    bool              m_active        = false;
    bool              m_busy          = false;
    std::string       m_projectDir;
    std::string       m_sessionFile;
    ExamConfig        m_cfg;
    LLMConfig         m_llmCfg;
    bool              m_darkMode      = false;
    int               m_questionIndex = 0;
    std::string       m_currentQuestion;
    std::vector<QuestionTurn> m_turns;

    wxWebView*  m_webView    = nullptr;
    wxTextCtrl* m_answerCtrl = nullptr;
    wxButton*   m_sendBtn    = nullptr;
    wxButton*   m_skipBtn    = nullptr;
    wxButton*   m_flagBtn    = nullptr;
    wxStaticText* m_statusLabel = nullptr;

    void RequestFirstQuestion();
    void SubmitAnswer(const std::string& answer);
    void Render();
    std::string BuildExamHTML() const;

    void OnSend(wxCommandEvent&);
    void OnSkip(wxCommandEvent&);
    void OnFlag(wxCommandEvent&);

    wxDECLARE_EVENT_TABLE();
};
