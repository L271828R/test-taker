#pragma once
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <wx/wx.h>
#include <wx/webview.h>

class PersonaTab : public wxPanel {
public:
    using RenameCallback = std::function<void(const std::string& oldName,
                                              const std::string& newName)>;

    explicit PersonaTab(wxWindow* parent, bool darkMode = false);

    void SetDarkMode(bool dark);
    void Activate();
    void FlushPendingDescs();

    void SetOnRenamePersona(RenameCallback cb) { m_onRename = std::move(cb); }

    std::map<std::string, std::vector<std::string>> GetLibrary() const;
    std::set<std::string>                            GetCheckedPersonas() const;

private:
    wxWebView*     m_webView         = nullptr;
    bool           m_darkMode        = false;
    bool           m_ready           = false;
    bool           m_pendingActivate = false;
    RenameCallback m_onRename;

    std::map<std::string, std::vector<std::string>> m_personasByCategory;
    std::set<std::string>                            m_checkedPersonas;
    std::map<std::string, std::string>               m_personaDescs;

    void Run(const std::string& js);
    void HandleMessage(const std::string& json);
    void LoadState();
    void SaveState() const;
    void PushState();

    void DoToggle(const std::string& name, bool checked);
    void DoSetDesc(const std::string& name, const std::string& desc);
    void DoAddCategory(const std::string& name);
    void DoDeleteCategory(const std::string& name);
    void DoAddPersona(const std::string& cat, const std::string& name);
    void DoDeletePersona(const std::string& cat, const std::string& name);
    void DoUploadImage(const std::string& name);
    void DoRenamePersona(const std::string& oldName, const std::string& newName);
};
