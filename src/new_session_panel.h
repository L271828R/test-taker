#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <wx/wx.h>
#include <wx/webview.h>
#include <wx/app.h>
#include "new_session_html.h"
#include "exam_prompt.h"
#include "llm.h"

class NewSessionPanel : public wxPanel {
public:
    using StartCallback = std::function<void(const std::string& projectDir,
                                             const std::string& sessionFile,
                                             const ExamConfig&  cfg,
                                             const LLMConfig&   llmCfg)>;

    NewSessionPanel(wxWindow* parent, bool darkMode, StartCallback onSessionStarted);

    // Called when a project is activated in the Projects tab.
    void SyncProject(const std::string& projectDir);

    // Pre-fill session fields from the deep-dive dialog.
    void PreFill(const std::string& topic, const std::vector<FocusArea>& focusAreas,
                 const std::string& difficulty, int questionCount);

    void SaveFormState() const;
    void SetDarkMode(bool dark);

private:
    StartCallback       m_onStart;
    std::string         m_activeProjectDir;
    bool                m_darkMode  = false;
    wxWebView*          m_webView   = nullptr;
    NewSessionFormState m_state;

    void Render();
    void LoadPersonalityLibrary();
    void SavePersonalityLibrary() const;

    void HandleStart(const std::string& payload);
    void HandleRefreshOllama();
    void HandleOpenContext();
    void HandleResetWeights();
    void HandleAddPersonality(const std::string& payload);
    void HandleDeletePersonality(const std::string& payload);

    std::string ExtractFormJSON() const;
    std::string GenerateSessionFilename() const;

    void OnNsAction(wxWebViewEvent&);

    wxDECLARE_EVENT_TABLE();
};
