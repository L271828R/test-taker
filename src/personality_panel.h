#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include <wx/wx.h>
#include <wx/checklst.h>

// Two-panel personality library editor.
// Left: category list. Right: characters in selected category (checkboxes).
// Checked state persists across category switches via m_checkedChars.
// Library (categories + characters) saved globally via wxConfig("TestTaker").
// Checked state is retrieved/restored via GetSelected()/SetSelected().
class PersonalityPickerPanel : public wxPanel {
public:
    explicit PersonalityPickerPanel(wxWindow* parent);

    std::vector<std::string> GetSelected() const;
    void SetSelected(const std::vector<std::string>& names);

private:
    wxListBox*      m_catList  = nullptr;
    wxCheckListBox* m_charList = nullptr;

    std::map<std::string, std::vector<std::string>> m_charsByCategory;
    std::set<std::string> m_checkedChars;

    void LoadLibrary();
    void SaveLibrary() const;
    void RefreshCharList();
    std::string SelectedCategory() const;

    void OnCatSelected(wxCommandEvent&);
    void OnCharToggled(wxCommandEvent&);
    void OnAddCategory(wxCommandEvent&);
    void OnDeleteCategory(wxCommandEvent&);
    void OnAddCharacter(wxCommandEvent&);
    void OnDeleteCharacter(wxCommandEvent&);

    wxDECLARE_EVENT_TABLE();
};
