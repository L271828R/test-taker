#pragma once
#include <wx/wx.h>
#include <wx/webview.h>
#include <functional>
#include <string>
#include <vector>
#include "turn_chat.h"
#include "session.h"
#include "llm.h"
#include "corpus.h"

class TurnChatPanel : public wxPanel {
public:
    // onClose is called when the user clicks the × button; caller should unsplit.
    TurnChatPanel(wxWindow* parent, bool darkMode, std::function<void()> onClose);

    // Load the context for a specific exam turn and show the panel.
    // turnIndex is the zero-based index into m_turns / the session file.
    void OpenTurn(const QuestionTurn& turn,
                  int                 turnIndex,
                  const std::string&  sessionFile,
                  const LLMConfig&    llmCfg);

    // Clear state (called when a new session starts).
    void Reset();

private:
    bool                  m_darkMode;
    std::function<void()> m_onClose;
    int                   m_turnIndex  = -1;
    std::string           m_sessionFile;
    std::string           m_projectDir;
    LLMConfig             m_llmCfg;
    QuestionTurn          m_examTurn;
    bool                  m_busy       = false;

    std::vector<TurnChatTurn> m_turns;

    wxWebView*    m_webView    = nullptr;
    wxTextCtrl*   m_inputCtrl  = nullptr;
    wxButton*     m_sendBtn    = nullptr;
    wxButton*     m_closeBtn   = nullptr;
    wxStaticText* m_titleLabel = nullptr;

    void Render(const std::string& pendingQuestion = "");
    std::string BuildChatHTML(const std::string& pendingQuestion) const;

    void OnSend(wxCommandEvent&);
    void OnClose(wxCommandEvent&);

    wxDECLARE_EVENT_TABLE();
};
