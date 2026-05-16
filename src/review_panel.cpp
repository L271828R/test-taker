#include "review_panel.h"
#include <filesystem>
#include <wx/splitter.h>

namespace fs = std::filesystem;

enum { ID_FLAGGED_LIST = wxID_HIGHEST + 200 };

wxBEGIN_EVENT_TABLE(ReviewPanel, wxPanel)
    EVT_LIST_ITEM_ACTIVATED(ID_FLAGGED_LIST, ReviewPanel::OnFlaggedActivated)
wxEND_EVENT_TABLE()

ReviewPanel::ReviewPanel(wxWindow* parent, DrillCallback onDrillRequested)
    : wxPanel(parent), m_onDrill(std::move(onDrillRequested))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    m_summaryLabel = new wxStaticText(this, wxID_ANY, "No project active.");
    outer->Add(m_summaryLabel, 0, wxALL, 8);

    auto* splitter = new wxSplitterWindow(this, wxID_ANY,
        wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);

    // ── Top: session list ─────────────────────────────────────────────────
    auto* topPanel  = new wxPanel(splitter);
    auto* topSizer  = new wxBoxSizer(wxVERTICAL);
    topSizer->Add(new wxStaticText(topPanel, wxID_ANY, "Past sessions:"), 0, wxALL, 4);
    m_sessionList = new wxListCtrl(topPanel, wxID_ANY,
        wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_sessionList->InsertColumn(0, "Date",       wxLIST_FORMAT_LEFT, 160);
    m_sessionList->InsertColumn(1, "Topic",      wxLIST_FORMAT_LEFT, 220);
    m_sessionList->InsertColumn(2, "Score",      wxLIST_FORMAT_LEFT, 100);
    m_sessionList->InsertColumn(3, "Difficulty", wxLIST_FORMAT_LEFT,  80);
    m_sessionList->InsertColumn(4, "Flagged",    wxLIST_FORMAT_LEFT,  60);
    topSizer->Add(m_sessionList, 1, wxEXPAND | wxALL, 4);
    topPanel->SetSizer(topSizer);

    // ── Bottom: flagged questions ─────────────────────────────────────────
    auto* botPanel  = new wxPanel(splitter);
    auto* botSizer  = new wxBoxSizer(wxVERTICAL);
    botSizer->Add(new wxStaticText(botPanel, wxID_ANY,
        "Flagged questions (double-click to re-drill):"), 0, wxALL, 4);
    m_flaggedList = new wxListCtrl(botPanel, ID_FLAGGED_LIST,
        wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_flaggedList->InsertColumn(0, "Question", wxLIST_FORMAT_LEFT, 400);
    m_flaggedList->InsertColumn(1, "Prev result", wxLIST_FORMAT_LEFT, 100);
    m_flaggedList->InsertColumn(2, "Session",  wxLIST_FORMAT_LEFT, 180);
    botSizer->Add(m_flaggedList, 1, wxEXPAND | wxALL, 4);
    botPanel->SetSizer(botSizer);

    splitter->SplitHorizontally(topPanel, botPanel, 200);
    outer->Add(splitter, 1, wxEXPAND);
    SetSizer(outer);
}

// ---------------------------------------------------------------------------
void ReviewPanel::RefreshSessions(const std::string& projectDir) {
    m_projectDir = projectDir;
    LoadSessionList();
    LoadFlaggedList();
}

void ReviewPanel::LoadSessionList() {
    m_sessionList->DeleteAllItems();
    if (m_projectDir.empty()) return;

    auto meta = LoadExamMeta(m_projectDir);
    int total = 0, correct = 0, flagged = 0;

    for (int i = (int)meta.sessions.size() - 1; i >= 0; --i) {
        const auto& s = meta.sessions[i];
        long row = m_sessionList->InsertItem(0, s.startedAt.substr(0, 16));
        m_sessionList->SetItem(row, 1, s.topic);
        std::string score = std::to_string(s.correct) + "/" + std::to_string(s.totalQuestions);
        m_sessionList->SetItem(row, 2, score);
        m_sessionList->SetItem(row, 3, s.difficulty);
        m_sessionList->SetItem(row, 4, std::to_string(s.flaggedCount));
        total   += s.totalQuestions;
        correct += s.correct;
        flagged += s.flaggedCount;
    }

    std::string summary = std::to_string(meta.sessions.size()) + " sessions  |  "
        + std::to_string(correct) + "/" + std::to_string(total) + " correct overall  |  "
        + std::to_string(flagged) + " flagged for review";
    m_summaryLabel->SetLabel(summary);
}

void ReviewPanel::LoadFlaggedList() {
    m_flaggedList->DeleteAllItems();
    m_flaggedRows.clear();
    if (m_projectDir.empty()) return;

    std::error_code ec;
    for (auto& entry : fs::directory_iterator(m_projectDir, ec)) {
        if (entry.path().extension() != ".md") continue;
        std::string path = entry.path().string();
        auto turns = LoadSession(path);
        for (int i = 0; i < (int)turns.size(); ++i) {
            if (!turns[i].flagged) continue;
            FlaggedRow fr;
            fr.sessionFile   = path;
            fr.questionIndex = i;
            fr.questionText  = turns[i].question;
            fr.previousScore = turns[i].score;
            m_flaggedRows.push_back(fr);

            long row = m_flaggedList->InsertItem(
                m_flaggedList->GetItemCount(),
                fr.questionText.substr(0, 80));
            m_flaggedList->SetItem(row, 1, ScoreLabel(fr.previousScore));
            m_flaggedList->SetItem(row, 2, entry.path().filename().string());
        }
    }
}

// ---------------------------------------------------------------------------
void ReviewPanel::OnFlaggedActivated(wxListEvent& evt) {
    long idx = evt.GetIndex();
    if (idx < 0 || idx >= (long)m_flaggedRows.size()) return;
    const auto& fr = m_flaggedRows[idx];
    if (m_onDrill) m_onDrill(m_projectDir, fr.sessionFile, fr.questionIndex);
}
