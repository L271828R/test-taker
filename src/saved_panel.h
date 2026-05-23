#pragma once
#include <functional>
#include <string>
#include <wx/wx.h>
#include <wx/webview.h>
#include <wx/splitter.h>
#include "llm.h"
#include "turn_chat_panel.h"

class SavedPanel : public wxPanel {
public:
    SavedPanel(wxWindow* parent, bool darkMode);

    void SyncProject(const std::string& projectDir,
                     const LLMConfig&   llmCfg,
                     bool               darkMode);
    void Refresh();
    void SetDarkMode(bool dark);

private:
    std::string       m_projectDir;
    LLMConfig         m_llmCfg;
    bool              m_darkMode  = false;
    bool              m_chatOpen  = false;
    int               m_currentIdx = -1;

    wxSplitterWindow* m_splitter   = nullptr;
    wxPanel*          m_leftPanel  = nullptr;
    TurnChatPanel*    m_chatPanel  = nullptr;
    wxWebView*        m_webView    = nullptr;
    wxButton*         m_exportBtn  = nullptr;

    void Render();
    void OpenChatFor(int fileIdx,
                     const std::string& starterMsg      = "",
                     const std::string& starterDisplayQ = "");
    void LaunchGame(int fileIdx,
                    const std::string& question,
                    const std::string& explanation);

    void OnExport(wxCommandEvent&);
    void OnWebViewNav(wxWebViewEvent&);

    wxDECLARE_EVENT_TABLE();
};
