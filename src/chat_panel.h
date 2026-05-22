#pragma once
#include <functional>
#include <set>
#include <string>
#include <vector>
#include <wx/wx.h>
#include <wx/webview.h>
#include "conversation.h"
#include "llm.h"

class ChatPanel : public wxPanel {
public:
    using SavedConvoCallback = std::function<void()>;

    ChatPanel(wxWindow* parent, bool darkMode,
              SavedConvoCallback onSavedConvo = {});

    // Called when a project is activated — loads chat.md for that project.
    void SyncProject(const std::string& projectDir,
                     const LLMConfig&   llmCfg,
                     bool               darkMode);
    void SetDarkMode(bool dark) { m_darkMode = dark; Render(); }

private:
    SavedConvoCallback       m_onSavedConvo;
    std::string              m_projectDir;
    std::string              m_chatFile;   // <projectDir>/chat.md
    LLMConfig                m_llmCfg;
    std::vector<std::string> m_personalities;
    bool                     m_darkMode = false;
    bool                     m_busy     = false;
    std::vector<ConversationTurn> m_turns;
    std::set<int>            m_savedIndices;  // tracks which turns saved this session

    wxWebView*  m_webView   = nullptr;
    wxTextCtrl* m_inputCtrl = nullptr;
    wxButton*   m_sendBtn   = nullptr;
    wxButton*   m_clearBtn  = nullptr;

    void LoadHistory();
    void Render(const std::string& pendingQuestion = "");
    std::string BuildChatHTML(const std::string& pendingQ = "") const;
    void FireAsNewTurn(const std::string& displayQuestion, const std::string& prompt);

    void OnSend(wxCommandEvent&);
    void OnClearChat(wxCommandEvent&);
    void OnWebViewNav(wxWebViewEvent&);

    wxDECLARE_EVENT_TABLE();
};
