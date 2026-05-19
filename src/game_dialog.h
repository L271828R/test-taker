#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <wx/dialog.h>
#include <wx/webview.h>
#include "llm.h"

class GameDialog : public wxDialog {
public:
    GameDialog(wxWindow* parent,
               const std::string& question,
               const std::string& explanation,
               const LLMConfig&   llmCfg);
    ~GameDialog();

private:
    void GenerateChoices();
    void StartGame(const std::string& correct, const std::string& wrong);
    void OnClose(wxCloseEvent&);

    wxWebView*  m_webView    = nullptr;
    LLMConfig   m_llmCfg;
    std::string m_question;
    std::string m_explanation;

    std::shared_ptr<std::atomic<bool>> m_cancel;
    std::thread                         m_worker;

    wxDECLARE_EVENT_TABLE();
};
