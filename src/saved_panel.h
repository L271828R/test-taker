#pragma once
#include <string>
#include <wx/wx.h>
#include <wx/webview.h>

class SavedPanel : public wxPanel {
public:
    SavedPanel(wxWindow* parent, bool darkMode);

    void SyncProject(const std::string& projectDir, bool darkMode);
    void Refresh();
    void SetDarkMode(bool dark) { m_darkMode = dark; Render(); }

private:
    std::string m_projectDir;
    bool        m_darkMode = false;

    wxWebView* m_webView   = nullptr;
    wxButton*  m_exportBtn = nullptr;

    void Render();
    void OnExport(wxCommandEvent&);
    void OnWebViewNav(wxWebViewEvent&);

    wxDECLARE_EVENT_TABLE();
};
