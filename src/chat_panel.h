#pragma once
#include <functional>
#include <string>
#include <vector>
#include <wx/wx.h>
#include <wx/webview.h>
#include "conversation.h"
#include "llm.h"

class ChatPanel : public wxPanel {
public:
    ChatPanel(wxWindow* parent, bool darkMode);

    // Called when a project is activated — loads chat.md for that project.
    void SyncProject(const std::string& projectDir,
                     const LLMConfig&   llmCfg,
                     bool               darkMode);

private:
    std::string              m_chatFile;   // <projectDir>/chat.md
    LLMConfig                m_llmCfg;
    bool                     m_darkMode = false;
    bool                     m_busy     = false;
    std::vector<ConversationTurn> m_turns;

    wxWebView*  m_webView   = nullptr;
    wxTextCtrl* m_inputCtrl = nullptr;
    wxButton*   m_sendBtn   = nullptr;

    void LoadHistory();
    void Render(const std::string& pendingQuestion = "");
    std::string BuildChatHTML(const std::string& pendingQ = "") const;

    void OnSend(wxCommandEvent&);

    wxDECLARE_EVENT_TABLE();
};
