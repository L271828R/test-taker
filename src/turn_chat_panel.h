#pragma once
#include <wx/wx.h>
#include <wx/webview.h>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "turn_chat.h"
#include "session.h"
#include "llm.h"
#include "corpus.h"

class TurnChatPanel : public wxPanel {
public:
    using SavedConvoCallback = std::function<void()>;

    // onClose is called when the user clicks the × button; caller should unsplit.
    TurnChatPanel(wxWindow* parent, bool darkMode,
                  std::function<void()> onClose,
                  SavedConvoCallback    onSavedConvo = {});

    // Load the context for a specific exam turn and show the panel.
    // turnIndex is the zero-based index into m_turns / the session file.
    // starterMessage: auto-fired LLM prompt on open (shown as starterDisplayQ bubble).
    // starterDisplayQ: label for the auto-fired starter bubble (defaults to monkey/banana label).
    void OpenTurn(const QuestionTurn& turn,
                  int                 turnIndex,
                  const std::string&  sessionFile,
                  const LLMConfig&    llmCfg,
                  const std::string&  starterMessage  = "",
                  const std::string&  starterDisplayQ = "");

    // Clear state (called when a new session starts).
    void Reset();

    void SetDarkMode(bool dark);

private:
    bool                  m_darkMode;
    std::function<void()> m_onClose;
    SavedConvoCallback    m_onSavedConvo;
    std::set<int>         m_savedIndices;
    int                   m_turnIndex  = -1;
    std::string           m_sessionFile;
    std::string           m_projectDir;
    LLMConfig             m_llmCfg;
    QuestionTurn          m_examTurn;
    bool                  m_busy       = false;

    std::vector<TurnChatTurn>              m_turns;
    std::map<std::string, std::string>    m_thumbnails;

    wxWebView*    m_webView    = nullptr;
    wxButton*     m_closeBtn   = nullptr;
    wxStaticText* m_titleLabel = nullptr;

    void Render(const std::string& pendingQuestion = "");
    std::string BuildChatHTML(const std::string& pendingQuestion) const;

    void OnChatAction(wxWebViewEvent&);
    void OnClose(wxCommandEvent&);
    void OnWebViewNav(wxWebViewEvent&);

    wxDECLARE_EVENT_TABLE();
};
