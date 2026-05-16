#pragma once
#include <wx/wx.h>
#include <wx/webview.h>
#include <functional>
#include <string>
#include <vector>
#include "conversation.h"
#include "llm.h"

class ChatFrame : public wxFrame {
public:
    // onConversationSaved is called (on the main thread) each time a turn is
    // persisted to disk, so the caller can re-render the document.
    ChatFrame(wxWindow*          parent,
              const std::string& filePath,
              int                chId,
              const std::string& chTitle,
              const LLMConfig&   llmCfg,
              bool               darkMode,
              std::function<void()> onConversationSaved);

private:
    std::string              m_filePath;
    int                      m_chId;
    std::string              m_chTitle;
    LLMConfig                m_llmCfg;
    bool                     m_darkMode;
    std::function<void()>    m_onSaved;
    std::vector<ConversationTurn> m_turns;
    bool                     m_busy = false;

    wxWebView*  m_webView;
    wxTextCtrl* m_inputCtrl;
    wxButton*   m_sendBtn;

    void LoadHistory();
    void Render(const std::string& pendingQuestion = "");
    void OnSend(wxCommandEvent&);
    void OnScriptMessage(wxWebViewEvent&);

    wxDECLARE_EVENT_TABLE();
};
