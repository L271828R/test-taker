#pragma once
#include <functional>
#include <set>
#include <string>
#include <wx/wx.h>
#include <wx/webview.h>
#include <wx/app.h>
#include "project_panel_html.h"

class ProjectPanel : public wxPanel {
public:
    using OpenCallback = std::function<void(const std::string&)>;

    ProjectPanel(wxWindow* parent, OpenCallback onProjectActivated);

    void RefreshProjects();
    void SetDarkMode(bool dark);

private:
    OpenCallback        m_openCallback;
    wxWebView*          m_webView     = nullptr;
    ProjectPanelState   m_state;
    bool                m_startupDone = false;

    void Render();
    void BuildTree();   // populates m_state.tree from filesystem, calls Render()

    void HandleActivate(const std::string& path);
    void HandleRename(const std::string& path);
    void HandleDelete(const std::string& path);
    void HandleNewProject(const std::string& parentPath);
    void HandleNewSubfolder(const std::string& parentPath);
    void HandleSetFolder();
    void HandleMove(const std::string& src, const std::string& dst);
    void HandleSort(const std::string& order);

    void OnPpAction(wxWebViewEvent&);
};
