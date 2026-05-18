#pragma once
#include <functional>
#include <string>
#include <wx/wx.h>
#include <wx/spinctrl.h>
#include <wx/combobox.h>
#include <wx/statline.h>
#include "exam_prompt.h"
#include "focus_list_panel.h"
#include "llm.h"

class NewSessionPanel : public wxPanel {
public:
    using StartCallback = std::function<void(const std::string& projectDir,
                                             const std::string& sessionFile,
                                             const ExamConfig&  cfg,
                                             const LLMConfig&   llmCfg)>;

    NewSessionPanel(wxWindow* parent, StartCallback onSessionStarted);

    // Called when a project is activated in the Projects tab.
    void SyncProject(const std::string& projectDir);

    // Pre-fill session fields from the 🎯 deep-dive dialog.
    void PreFill(const std::string&          topic,
                 const std::vector<FocusArea>& focusAreas,
                 const std::string&          difficulty,
                 int                         questionCount);

    void SaveFormState() const;

private:
    StartCallback m_onStart;
    std::string   m_activeProjectDir;

    wxStaticText* m_projectLabel   = nullptr;
    wxTextCtrl*   m_topicCtrl      = nullptr;  // short label (one line)
    wxTextCtrl*     m_instrCtrl      = nullptr;  // free-form focus instructions
    FocusListPanel* m_focusListPanel = nullptr;  // weighted focus-area list
    wxChoice*     m_difficultyCtrl = nullptr;
    wxSpinCtrl*   m_countCtrl      = nullptr;
    wxChoice*     m_backendChoice  = nullptr;
    wxTextCtrl*   m_apiKeyCtrl     = nullptr;
    wxComboBox*   m_ollamaModel    = nullptr;
    wxSizerItem*  m_apiKeySizer    = nullptr;
    wxSizerItem*  m_ollamaSizer    = nullptr;
    wxButton*     m_startBtn        = nullptr;
    wxTextCtrl*   m_statusCtrl     = nullptr;
    wxCheckBox*   m_useCorpusCheck = nullptr;
    wxSizerItem*  m_corpusSizer    = nullptr;

    void UpdateBackendFields();
    void RestoreFormState();
    std::string GenerateSessionFilename() const;
    void SetStatus(const wxString& msg);

    void OnStart(wxCommandEvent&);
    void OnBackendChanged(wxCommandEvent&);
    void OnOpenContext(wxCommandEvent&);

    wxDECLARE_EVENT_TABLE();
};
