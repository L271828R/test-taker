#pragma once
#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <vector>
#include "exam_prompt.h"

// A scrollable list of (focus-area text, star-priority) rows.
// Use GetAreas() / SetAreas() to read/write the list contents.
class FocusListPanel : public wxScrolledWindow {
public:
    FocusListPanel(wxWindow* parent, int minHeight = 140);

    void SetAreas(const std::vector<FocusArea>& areas);
    std::vector<FocusArea> GetAreas() const;

private:
    struct Row {
        wxTextCtrl* textCtrl  = nullptr;
        wxChoice*   stars     = nullptr;
        wxButton*   removeBtn = nullptr;
        wxSizer*    sizer     = nullptr;
    };

    wxBoxSizer* m_listSizer = nullptr;
    std::vector<Row> m_rows;

    void AddRow(const std::string& text = "", int stars = 3);
    void RemoveRow(wxButton* btn);
    void Relayout();
};
