#include "personality_panel.h"
#include "personality_lib.h"
#include <wx/config.h>
#include <wx/tokenzr.h>
#include <algorithm>

enum {
    ID_PP_CAT_LIST  = wxID_HIGHEST + 400,
    ID_PP_CHAR_LIST,
    ID_PP_ADD_CAT,
    ID_PP_DEL_CAT,
    ID_PP_ADD_CHAR,
    ID_PP_DEL_CHAR,
};

wxBEGIN_EVENT_TABLE(PersonalityPickerPanel, wxPanel)
    EVT_LISTBOX    (ID_PP_CAT_LIST,  PersonalityPickerPanel::OnCatSelected)
    EVT_CHECKLISTBOX(ID_PP_CHAR_LIST, PersonalityPickerPanel::OnCharToggled)
    EVT_BUTTON(ID_PP_ADD_CAT,  PersonalityPickerPanel::OnAddCategory)
    EVT_BUTTON(ID_PP_DEL_CAT,  PersonalityPickerPanel::OnDeleteCategory)
    EVT_BUTTON(ID_PP_ADD_CHAR, PersonalityPickerPanel::OnAddCharacter)
    EVT_BUTTON(ID_PP_DEL_CHAR, PersonalityPickerPanel::OnDeleteCharacter)
wxEND_EVENT_TABLE()

PersonalityPickerPanel::PersonalityPickerPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    auto* cols = new wxBoxSizer(wxHORIZONTAL);

    // Left: category list + add/delete buttons
    auto* leftCol = new wxBoxSizer(wxVERTICAL);
    m_catList = new wxListBox(this, ID_PP_CAT_LIST,
                              wxDefaultPosition, wxSize(130, 148));
    leftCol->Add(m_catList, 1, wxEXPAND | wxBOTTOM, 4);
    auto* catBtns = new wxBoxSizer(wxHORIZONTAL);
    catBtns->Add(new wxButton(this, ID_PP_ADD_CAT, "+ Category",
                              wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 4);
    catBtns->Add(new wxButton(this, ID_PP_DEL_CAT, "✕",
                              wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
    leftCol->Add(catBtns, 0);
    cols->Add(leftCol, 0, wxEXPAND | wxRIGHT, 10);

    // Right: character checklist + add/delete buttons
    auto* rightCol = new wxBoxSizer(wxVERTICAL);
    m_charList = new wxCheckListBox(this, ID_PP_CHAR_LIST,
                                    wxDefaultPosition, wxSize(-1, 148));
    rightCol->Add(m_charList, 1, wxEXPAND | wxBOTTOM, 4);
    auto* charBtns = new wxBoxSizer(wxHORIZONTAL);
    charBtns->Add(new wxButton(this, ID_PP_ADD_CHAR, "+ Character",
                               wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 4);
    charBtns->Add(new wxButton(this, ID_PP_DEL_CHAR, "✕",
                               wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
    rightCol->Add(charBtns, 0);
    cols->Add(rightCol, 1, wxEXPAND);

    SetSizer(cols);
    LoadLibrary();
}

// ── Library persistence ───────────────────────────────────────────────────────

void PersonalityPickerPanel::LoadLibrary() {
    wxConfig cfg("TestTaker");
    cfg.SetPath("/charlib");

    wxString catStr;
    if (!cfg.Read("categories", &catStr) || catStr.empty()) {
        m_charsByCategory = DefaultPersonalityLibrary();
    } else {
        wxStringTokenizer tok(catStr, ",");
        while (tok.HasMoreTokens()) {
            std::string cat = tok.GetNextToken().ToStdString();
            wxString charStr;
            cfg.Read(wxString::FromUTF8(cat), &charStr);
            auto& vec = m_charsByCategory[cat];
            wxStringTokenizer ctok(charStr, "|");
            while (ctok.HasMoreTokens())
                vec.push_back(ctok.GetNextToken().ToStdString());
        }
    }

    m_catList->Clear();
    for (auto& [cat, _] : m_charsByCategory)
        m_catList->Append(wxString::FromUTF8(cat));
    if (m_catList->GetCount() > 0) {
        m_catList->SetSelection(0);
        RefreshCharList();
    }
}

void PersonalityPickerPanel::SaveLibrary() const {
    wxConfig cfg("TestTaker");
    cfg.SetPath("/charlib");

    wxString catStr;
    for (auto& [cat, chars] : m_charsByCategory) {
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
}

// ── Helpers ───────────────────────────────────────────────────────────────────

std::string PersonalityPickerPanel::SelectedCategory() const {
    int sel = m_catList->GetSelection();
    if (sel == wxNOT_FOUND) return "";
    return m_catList->GetString(sel).ToStdString();
}

void PersonalityPickerPanel::RefreshCharList() {
    m_charList->Clear();
    std::string cat = SelectedCategory();
    auto it = m_charsByCategory.find(cat);
    if (it == m_charsByCategory.end()) return;
    for (auto& ch : it->second) {
        unsigned int idx = m_charList->Append(wxString::FromUTF8(ch));
        m_charList->Check(idx, m_checkedChars.count(ch) > 0);
    }
}

// ── Public interface ──────────────────────────────────────────────────────────

std::vector<std::string> PersonalityPickerPanel::GetSelected() const {
    return {m_checkedChars.begin(), m_checkedChars.end()};
}

void PersonalityPickerPanel::SetSelected(const std::vector<std::string>& names) {
    m_checkedChars = {names.begin(), names.end()};
    RefreshCharList();
}

// ── Event handlers ────────────────────────────────────────────────────────────

void PersonalityPickerPanel::OnCatSelected(wxCommandEvent&) {
    RefreshCharList();
}

void PersonalityPickerPanel::OnCharToggled(wxCommandEvent& evt) {
    unsigned int idx = (unsigned int)evt.GetInt();
    std::string name = m_charList->GetString(idx).ToStdString();
    if (m_charList->IsChecked(idx))
        m_checkedChars.insert(name);
    else
        m_checkedChars.erase(name);
}

void PersonalityPickerPanel::OnAddCategory(wxCommandEvent&) {
    wxString name = wxGetTextFromUser("Category name:", "Add Category", "", this).Trim();
    if (name.empty() || m_charsByCategory.count(name.ToStdString())) return;
    m_charsByCategory[name.ToStdString()];
    m_catList->Append(name);
    m_catList->SetSelection((int)m_catList->GetCount() - 1);
    RefreshCharList();
    SaveLibrary();
}

void PersonalityPickerPanel::OnDeleteCategory(wxCommandEvent&) {
    std::string cat = SelectedCategory();
    if (cat.empty()) return;
    if (wxMessageBox("Delete category \"" + cat + "\" and all its characters?",
                     "Confirm", wxYES_NO | wxNO_DEFAULT, this) != wxYES) return;
    m_charsByCategory.erase(cat);
    int sel = m_catList->GetSelection();
    m_catList->Delete((unsigned int)sel);
    if (m_catList->GetCount() > 0)
        m_catList->SetSelection(std::min(sel, (int)m_catList->GetCount() - 1));
    RefreshCharList();
    SaveLibrary();
}

void PersonalityPickerPanel::OnAddCharacter(wxCommandEvent&) {
    std::string cat = SelectedCategory();
    if (cat.empty()) return;
    wxString name = wxGetTextFromUser("Character name:", "Add Character", "", this).Trim();
    if (name.empty()) return;
    std::string ch = name.ToStdString();
    auto& vec = m_charsByCategory[cat];
    if (std::find(vec.begin(), vec.end(), ch) != vec.end()) return;
    vec.push_back(ch);
    unsigned int idx = m_charList->Append(name);
    m_charList->Check(idx, true);
    m_checkedChars.insert(ch);
    SaveLibrary();
}

void PersonalityPickerPanel::OnDeleteCharacter(wxCommandEvent&) {
    int idx = m_charList->GetSelection();
    if (idx == wxNOT_FOUND) return;
    std::string cat = SelectedCategory();
    std::string ch  = m_charList->GetString(idx).ToStdString();
    auto& vec = m_charsByCategory[cat];
    vec.erase(std::remove(vec.begin(), vec.end(), ch), vec.end());
    m_checkedChars.erase(ch);
    m_charList->Delete((unsigned int)idx);
    SaveLibrary();
}
