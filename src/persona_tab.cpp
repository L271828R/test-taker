#include "persona_tab.h"
#include "persona_tab_html.h"
#include "persona.h"
#include "personality_lib.h"
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <wx/config.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/tokenzr.h>

// ── JSON helpers ──────────────────────────────────────────────────────────────

static std::string PtField(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':')) ++pos;
    if (pos >= json.size() || json[pos] != '"') return "";
    ++pos;
    std::string val;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < json.size()) {
            char e = json[pos++];
            switch (e) {
                case '"':  val += '"';  break;
                case '\\': val += '\\'; break;
                case 'n':  val += '\n'; break;
                default:   val += e;    break;
            }
        } else val += c;
    }
    return val;
}

static bool PtBool(const std::string& json, const std::string& key) {
    for (const auto& pat : {"\"" + key + "\":", "\"" + key + "\": "}) {
        size_t pos = json.find(pat);
        if (pos == std::string::npos) continue;
        pos += pat.size();
        while (pos < json.size() && json[pos] == ' ') ++pos;
        return pos < json.size() && json[pos] == 't';
    }
    return false;
}

static std::string Jq(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if      (c == '"')  o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c < 0x20)  { char b[8]; snprintf(b, 8, "\\u%04x", c); o += b; }
        else                 o += (char)c;
    }
    return o + "\"";
}

// ── PersonaTab ────────────────────────────────────────────────────────────────

PersonaTab::PersonaTab(wxWindow* parent, bool darkMode)
    : wxPanel(parent, wxID_ANY), m_darkMode(darkMode)
{
    LoadState();

    m_webView = wxWebView::New(this, wxID_ANY);
    m_webView->AddScriptMessageHandler("personas");
    m_webView->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
        [this](wxWebViewEvent& evt) {
            HandleMessage(evt.GetString().ToStdString());
        }, wxID_ANY);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_webView, 1, wxEXPAND);
    SetSizer(sizer);

    m_webView->SetPage(wxString::FromUTF8(BuildPersonaTabHTML(darkMode)), "");
}

std::map<std::string, std::vector<std::string>> PersonaTab::GetLibrary() const {
    return m_personasByCategory;
}

std::set<std::string> PersonaTab::GetCheckedPersonas() const {
    return m_checkedPersonas;
}

void PersonaTab::Run(const std::string& js) {
    if (!m_ready) return;
    m_webView->RunScript(wxString::FromUTF8(js));
}

void PersonaTab::SetDarkMode(bool dark) {
    m_darkMode = dark;
    Run(std::string("setDarkMode(") + (dark ? "true" : "false") + ")");
}

void PersonaTab::Activate() {
    if (m_ready) PushState();
    else         m_pendingActivate = true;
}

// ── Persistence ───────────────────────────────────────────────────────────────

void PersonaTab::LoadState() {
    {
        wxConfig cfg("TestTaker");
        cfg.SetPath("/charlib");
        wxString catStr;
        if (!cfg.Read("categories", &catStr) || catStr.empty()) {
            m_personasByCategory = DefaultPersonalityLibrary();
        } else {
            m_personasByCategory.clear();
            wxStringTokenizer tok(catStr, ",");
            while (tok.HasMoreTokens()) {
                std::string cat = tok.GetNextToken().ToStdString();
                wxString charStr;
                cfg.Read(wxString::FromUTF8(cat), &charStr);
                auto& vec = m_personasByCategory[cat];
                wxStringTokenizer ctok(charStr, "|");
                while (ctok.HasMoreTokens())
                    vec.push_back(ctok.GetNextToken().ToStdString());
            }
        }
        wxString cs;
        if (cfg.Read("checked", &cs) && !cs.empty()) {
            wxStringTokenizer tok(cs, "|");
            while (tok.HasMoreTokens())
                m_checkedPersonas.insert(tok.GetNextToken().ToStdString());
        }
    }
    {
        wxConfig dcfg("TestTaker");
        dcfg.SetPath("/charlib_descriptions");
        wxString key, val;
        long idx = 0;
        if (dcfg.GetFirstEntry(key, idx)) {
            do {
                dcfg.Read(key, &val);
                m_personaDescs[key.ToStdString()] = val.ToStdString();
            } while (dcfg.GetNextEntry(key, idx));
        }
    }
}

void PersonaTab::SaveState() const {
    wxConfig cfg("TestTaker");
    cfg.SetPath("/charlib");

    wxString catStr;
    for (auto& [cat, chars] : m_personasByCategory) {
        if (!catStr.empty()) catStr += ",";
        catStr += wxString::FromUTF8(cat);
        wxString charStr;
        for (auto& ch : chars) {
            if (!charStr.empty()) charStr += "|";
            charStr += wxString::FromUTF8(ch);
        }
        cfg.Write(wxString::FromUTF8(cat), charStr);
    }
    cfg.Write("categories", catStr);

    wxString cs;
    for (auto& ch : m_checkedPersonas) {
        if (!cs.empty()) cs += "|";
        cs += wxString::FromUTF8(ch);
    }
    cfg.Write("checked", cs);

    cfg.DeleteGroup("/charlib_descriptions");
    wxConfig dcfg("TestTaker");
    dcfg.SetPath("/charlib_descriptions");
    for (auto& [name, desc] : m_personaDescs) {
        if (!desc.empty())
            dcfg.Write(wxString::FromUTF8(name), wxString::FromUTF8(desc));
    }
}

void PersonaTab::PushState() {
    if (!m_ready) return;

    auto images = ToDataURLs(ScanPersonaImages());

    std::ostringstream j;
    j << "{\"cats\":{";
    bool fc = true;
    for (auto& [cat, chars] : m_personasByCategory) {
        if (!fc) j << ",";
        fc = false;
        j << Jq(cat) << ":[";
        bool fn = true;
        for (auto& ch : chars) {
            if (!fn) j << ",";
            fn = false;
            j << Jq(ch);
        }
        j << "]";
    }
    j << "},\"imgs\":{";
    bool fi = true;
    for (auto& [k, v] : images) {
        if (!fi) j << ",";
        fi = false;
        j << Jq(k) << ":" << Jq(v);
    }
    j << "},\"checked\":[";
    bool fch = true;
    for (auto& ch : m_checkedPersonas) {
        if (!fch) j << ",";
        fch = false;
        j << Jq(ch);
    }
    j << "],\"descs\":{";
    bool fd = true;
    for (auto& [name, desc] : m_personaDescs) {
        if (!fd) j << ",";
        fd = false;
        j << Jq(name) << ":" << Jq(desc);
    }
    j << "}}";

    Run("setCharacters(" + j.str() + ")");
}

// ── Message dispatcher ────────────────────────────────────────────────────────

void PersonaTab::HandleMessage(const std::string& json) {
    std::string action = PtField(json, "action");
    if      (action == "ready")           { m_ready = true; if (m_pendingActivate) { m_pendingActivate = false; PushState(); } }
    else if (action == "toggle")          DoToggle(PtField(json,"name"), PtBool(json,"checked"));
    else if (action == "setDesc")         DoSetDesc(PtField(json,"name"), PtField(json,"description"));
    else if (action == "addCategory")     DoAddCategory(PtField(json,"name"));
    else if (action == "deleteCategory")  DoDeleteCategory(PtField(json,"name"));
    else if (action == "addCharacter")    DoAddPersona(PtField(json,"category"), PtField(json,"name"));
    else if (action == "deleteCharacter") DoDeletePersona(PtField(json,"category"), PtField(json,"name"));
    else if (action == "uploadImage")     DoUploadImage(PtField(json,"name"));
    else if (action == "renameCharacter") DoRenamePersona(PtField(json,"oldName"), PtField(json,"newName"));
}

// ── Action handlers ───────────────────────────────────────────────────────────

void PersonaTab::DoToggle(const std::string& name, bool checked) {
    if (checked) m_checkedPersonas.insert(name);
    else         m_checkedPersonas.erase(name);
    SaveState();
}

void PersonaTab::DoSetDesc(const std::string& name, const std::string& desc) {
    if (name.empty()) return;
    if (desc.empty()) m_personaDescs.erase(name);
    else              m_personaDescs[name] = desc;
    SaveState();
}

void PersonaTab::DoAddCategory(const std::string& name) {
    if (name.empty() || m_personasByCategory.count(name)) return;
    m_personasByCategory[name];
    SaveState();
    PushState();
}

void PersonaTab::DoDeleteCategory(const std::string& name) {
    if (name.empty()) return;
    auto it = m_personasByCategory.find(name);
    if (it == m_personasByCategory.end()) return;
    for (auto& ch : it->second) {
        m_checkedPersonas.erase(ch);
        m_personaDescs.erase(ch);
    }
    m_personasByCategory.erase(it);
    SaveState();
    PushState();
}

void PersonaTab::DoAddPersona(const std::string& cat, const std::string& name) {
    if (cat.empty() || name.empty()) return;
    auto& vec = m_personasByCategory[cat];
    if (std::find(vec.begin(), vec.end(), name) != vec.end()) return;
    vec.push_back(name);
    m_checkedPersonas.insert(name);
    SaveState();
    PushState();
}

void PersonaTab::DoDeletePersona(const std::string& cat, const std::string& name) {
    auto it = m_personasByCategory.find(cat);
    if (it != m_personasByCategory.end()) {
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), name), vec.end());
    }
    m_checkedPersonas.erase(name);
    m_personaDescs.erase(name);
    SaveState();
    PushState();
}

void PersonaTab::DoRenamePersona(const std::string& oldName, const std::string& newName) {
    if (oldName.empty() || newName.empty() || oldName == newName) return;

    for (auto& kv : m_personasByCategory) {
        auto& vec = kv.second;
        auto it = std::find(vec.begin(), vec.end(), oldName);
        if (it != vec.end()) { *it = newName; break; }
    }
    if (m_checkedPersonas.erase(oldName))
        m_checkedPersonas.insert(newName);
    auto dit = m_personaDescs.find(oldName);
    if (dit != m_personaDescs.end()) {
        m_personaDescs[newName] = std::move(dit->second);
        m_personaDescs.erase(dit);
    }

    RenamePersonaImage(oldName, newName);
    SaveState();
    PushState();

    if (m_onRename) m_onRename(oldName, newName);
}

void PersonaTab::DoUploadImage(const std::string& name) {
    if (name.empty()) return;
    wxFileDialog dlg(this,
        "Choose image for \"" + wxString::FromUTF8(name) + "\"",
        "", "",
        "Image files (*.jpg;*.jpeg;*.png;*.webp;*.gif)|*.jpg;*.jpeg;*.png;*.webp;*.gif",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_CANCEL) return;

    std::string dest = AddPersonaImage(name, dlg.GetPath().ToStdString());
    if (dest.empty()) {
        wxMessageBox("Could not save image.", "TestTaker", wxOK | wxICON_ERROR, this);
        return;
    }
    std::string key = NormalizePersonaName(name);
    auto converted = ToDataURLs({{key, "file://" + dest}});
    if (converted.empty()) return;
    const std::string& url = converted.begin()->second;
    m_webView->RunScript(
        "window._u=" + wxString::FromUTF8("\"" + url + "\";") +
        "updatePersonaImage(\"" + wxString::FromUTF8(key) + "\",window._u);" +
        "delete window._u;");
}
