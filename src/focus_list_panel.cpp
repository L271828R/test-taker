#include "focus_list_panel.h"

static const char* kStarLabels[] = {
    "\xe2\x98\x85",                                           // 1 ★
    "\xe2\x98\x85\xe2\x98\x85",                               // 2 ★★
    "\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85",                   // 3 ★★★
    "\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85",       // 4 ★★★★
    "\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85" // 5 ★★★★★
};

// ---------------------------------------------------------------------------
FocusListPanel::FocusListPanel(wxWindow* parent, int minHeight)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition,
                       wxSize(-1, minHeight), wxVSCROLL | wxBORDER_SIMPLE)
{
    SetScrollRate(0, 10);

    m_listSizer = new wxBoxSizer(wxVERTICAL);

    auto* addBtn = new wxButton(this, wxID_ANY, "+ Add area",
                                wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    addBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddRow(); });

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(m_listSizer, 0, wxEXPAND | wxBOTTOM, 2);
    outer->Add(addBtn, 0, wxTOP, 4);
    SetSizer(outer);
}

// ---------------------------------------------------------------------------
void FocusListPanel::AddRow(const std::string& text, int stars) {
    auto* rowSizer = new wxBoxSizer(wxHORIZONTAL);

    auto* textCtrl = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(text),
                                    wxDefaultPosition, wxDefaultSize);
    textCtrl->SetHint("Focus area, e.g. \"Presigned URLs\"");

    wxArrayString choices;
    for (auto* s : kStarLabels) choices.Add(wxString::FromUTF8(s));
    auto* starsChoice = new wxChoice(this, wxID_ANY, wxDefaultPosition,
                                     wxDefaultSize, choices);
    starsChoice->SetSelection(std::max(0, std::min(4, stars - 1)));

    auto* removeBtn = new wxButton(this, wxID_ANY, "\xc3\x97",
                                   wxDefaultPosition, wxSize(26, 26), wxBU_EXACTFIT);
    removeBtn->SetToolTip("Remove this focus area");
    removeBtn->Bind(wxEVT_BUTTON, [this, removeBtn](wxCommandEvent&) {
        RemoveRow(removeBtn);
    });

    rowSizer->Add(textCtrl,    1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    rowSizer->Add(starsChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    rowSizer->Add(removeBtn,   0, wxALIGN_CENTER_VERTICAL);

    m_rows.push_back({textCtrl, starsChoice, removeBtn, rowSizer});
    m_listSizer->Add(rowSizer, 0, wxEXPAND | wxBOTTOM, 4);
    Relayout();
}

// ---------------------------------------------------------------------------
void FocusListPanel::RemoveRow(wxButton* btn) {
    for (int i = 0; i < (int)m_rows.size(); ++i) {
        if (m_rows[i].removeBtn != btn) continue;
        auto& row = m_rows[i];
        m_listSizer->Detach(row.sizer);
        row.sizer->Clear(true); // destroys child windows (scheduled via Destroy())
        delete row.sizer;
        m_rows.erase(m_rows.begin() + i);
        Relayout();
        return;
    }
}

// ---------------------------------------------------------------------------
void FocusListPanel::Relayout() {
    if (GetSizer()) GetSizer()->Layout();
    FitInside();
    Refresh();
}

// ---------------------------------------------------------------------------
void FocusListPanel::SetAreas(const std::vector<FocusArea>& areas) {
    while (!m_rows.empty()) RemoveRow(m_rows[0].removeBtn);
    for (const auto& a : areas) AddRow(a.text, a.stars);
}

// ---------------------------------------------------------------------------
std::vector<FocusArea> FocusListPanel::GetAreas() const {
    std::vector<FocusArea> result;
    for (const auto& row : m_rows) {
        std::string text = row.textCtrl->GetValue().Trim().ToStdString();
        if (text.empty()) continue;
        int stars = row.stars->GetSelection() + 1;
        result.push_back({text, stars});
    }
    return result;
}
