#include "saved_panel.h"
#include "saved_convos.h"
#include "html_template.h"
#include <fstream>
#include <wx/filedlg.h>

enum { ID_SP_EXPORT = wxID_HIGHEST + 700 };

wxBEGIN_EVENT_TABLE(SavedPanel, wxPanel)
    EVT_BUTTON(ID_SP_EXPORT, SavedPanel::OnExport)
    EVT_WEBVIEW_NAVIGATING(wxID_ANY, SavedPanel::OnWebViewNav)
wxEND_EVENT_TABLE()

SavedPanel::SavedPanel(wxWindow* parent, bool darkMode)
    : wxPanel(parent), m_darkMode(darkMode)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    toolbar->AddStretchSpacer();
    m_exportBtn = new wxButton(this, ID_SP_EXPORT, "Export as Markdown…");
    toolbar->Add(m_exportBtn, 0, wxALL, 4);
    sizer->Add(toolbar, 0, wxEXPAND);

    m_webView = wxWebView::New(this, wxID_ANY, "about:blank");
    sizer->Add(m_webView, 1, wxEXPAND);
    SetSizer(sizer);
}

void SavedPanel::SyncProject(const std::string& projectDir, bool darkMode) {
    m_projectDir = projectDir;
    m_darkMode   = darkMode;
    Render();
}

void SavedPanel::Refresh() {
    Render();
}

void SavedPanel::Render() {
    auto convos = LoadSavedConvos(m_projectDir);
    std::string body = BuildSavedConvosHTML(convos);
    std::string html = BuildHTML(body, "Saved Convos", m_darkMode);
    m_webView->SetPage(wxString::FromUTF8(html), "");
    m_exportBtn->Enable(!convos.empty());
}

void SavedPanel::OnExport(wxCommandEvent&) {
    if (m_projectDir.empty()) return;

    wxFileDialog dlg(this, "Export Saved Convos", "", "saved_convos.md",
                     "Markdown files (*.md)|*.md",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;

    std::string src  = m_projectDir + "/saved_convos.md";
    std::string dest = dlg.GetPath().ToStdString();

    std::ifstream in(src, std::ios::binary);
    std::ofstream out(dest, std::ios::binary);
    if (!in || !out) {
        wxMessageBox("Could not export saved convos.", "Export Failed", wxOK | wxICON_ERROR);
        return;
    }
    out << in.rdbuf();
}

// ---------------------------------------------------------------------------
void SavedPanel::OnWebViewNav(wxWebViewEvent& evt) {
    wxString url = evt.GetURL();

    if (url.StartsWith("testtaker://delete-saved/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(25).ToLong(&idx);  // "testtaker://delete-saved/" is 25 chars
        DeleteSavedConvo(m_projectDir, (int)idx);
        Render();
        return;
    }

    evt.Skip();
}
