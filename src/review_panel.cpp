#include "review_panel.h"
#include <filesystem>
#include <wx/splitter.h>
#include <wx/menu.h>

namespace fs = std::filesystem;

enum {
    ID_SESSION_LIST  = wxID_HIGHEST + 200,
    ID_QUESTION_LIST,
    ID_TOGGLE_FLAG
};

wxBEGIN_EVENT_TABLE(ReviewPanel, wxPanel)
    EVT_LIST_ITEM_SELECTED(ID_SESSION_LIST,  ReviewPanel::OnSessionSelected)
    EVT_LIST_ITEM_ACTIVATED(ID_QUESTION_LIST, ReviewPanel::OnQuestionActivated)
    EVT_LIST_ITEM_RIGHT_CLICK(ID_QUESTION_LIST, ReviewPanel::OnQuestionRightClick)
    EVT_MENU(ID_TOGGLE_FLAG, ReviewPanel::OnToggleFlag)
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
    auto* topPanel = new wxPanel(splitter);
    auto* topSizer = new wxBoxSizer(wxVERTICAL);
    topSizer->Add(new wxStaticText(topPanel, wxID_ANY, "Past sessions:"), 0, wxALL, 4);
    m_sessionList = new wxListCtrl(topPanel, ID_SESSION_LIST,
        wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_sessionList->InsertColumn(0, "Date",       wxLIST_FORMAT_LEFT, 160);
    m_sessionList->InsertColumn(1, "Topic",      wxLIST_FORMAT_LEFT, 220);
    m_sessionList->InsertColumn(2, "Score",      wxLIST_FORMAT_LEFT, 100);
    m_sessionList->InsertColumn(3, "Difficulty", wxLIST_FORMAT_LEFT,  80);
    m_sessionList->InsertColumn(4, "Flagged",    wxLIST_FORMAT_LEFT,  60);
    topSizer->Add(m_sessionList, 1, wxEXPAND | wxALL, 4);
    topPanel->SetSizer(topSizer);

    // ── Bottom: questions for selected session ────────────────────────────
    auto* botPanel = new wxPanel(splitter);
    auto* botSizer = new wxBoxSizer(wxVERTICAL);
    botSizer->Add(new wxStaticText(botPanel, wxID_ANY,
        "Questions (double-click to re-drill, right-click to toggle flag):"), 0, wxALL, 4);
    m_questionList = new wxListCtrl(botPanel, ID_QUESTION_LIST,
        wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_questionList->InsertColumn(0, "",          wxLIST_FORMAT_LEFT,  20);  // flag marker
    m_questionList->InsertColumn(1, "Question",  wxLIST_FORMAT_LEFT, 380);
    m_questionList->InsertColumn(2, "Result",    wxLIST_FORMAT_LEFT, 100);
    botSizer->Add(m_questionList, 1, wxEXPAND | wxALL, 4);
    botPanel->SetSizer(botSizer);

    splitter->SplitHorizontally(topPanel, botPanel, 200);
    outer->Add(splitter, 1, wxEXPAND);
    SetSizer(outer);
}

// ---------------------------------------------------------------------------
void ReviewPanel::RefreshSessions(const std::string& projectDir) {
    m_projectDir = projectDir;
    LoadSessionList();
    m_questionList->DeleteAllItems();
    m_questionRows.clear();
}

void ReviewPanel::LoadSessionList() {
    m_sessionList->DeleteAllItems();
    m_sessionRecords.clear();
    if (m_projectDir.empty()) return;

    auto meta = LoadExamMeta(m_projectDir);
    int total = 0, correct = 0, flagged = 0;

    for (int i = (int)meta.sessions.size() - 1; i >= 0; --i) {
        const auto& s = meta.sessions[i];
        long row = m_sessionList->InsertItem(
            m_sessionList->GetItemCount(), s.startedAt.substr(0, 16));
        m_sessionList->SetItem(row, 1, s.topic);
        m_sessionList->SetItem(row, 2,
            std::to_string(s.correct) + "/" + std::to_string(s.totalQuestions));
        m_sessionList->SetItem(row, 3, s.difficulty);
        m_sessionList->SetItem(row, 4, std::to_string(s.flaggedCount));
        m_sessionRecords.push_back(s);
        total   += s.totalQuestions;
        correct += s.correct;
        flagged += s.flaggedCount;
    }

    m_summaryLabel->SetLabel(
        std::to_string(meta.sessions.size()) + " sessions  |  "
        + std::to_string(correct) + "/" + std::to_string(total) + " correct overall  |  "
        + std::to_string(flagged) + " flagged for review");
}

void ReviewPanel::LoadQuestionList(const std::string& sessionFile) {
    m_questionList->DeleteAllItems();
    m_questionRows.clear();
    if (sessionFile.empty()) return;

    auto turns = LoadSession(sessionFile);
    for (int i = 0; i < (int)turns.size(); ++i) {
        QuestionRow qr;
        qr.sessionFile   = sessionFile;
        qr.questionIndex = i;
        qr.questionText  = turns[i].question;
        qr.score         = turns[i].score;
        qr.flagged       = turns[i].flagged;
        m_questionRows.push_back(qr);

        long row = m_questionList->InsertItem(
            m_questionList->GetItemCount(),
            qr.flagged ? wxString::FromUTF8("⚑") : wxString(""));
        m_questionList->SetItem(row, 1, wxString::FromUTF8(
            qr.questionText.size() > 100
                ? qr.questionText.substr(0, 100) + "…"
                : qr.questionText));
        m_questionList->SetItem(row, 2, ScoreLabel(qr.score));
    }
}

// ---------------------------------------------------------------------------
void ReviewPanel::OnSessionSelected(wxListEvent& evt) {
    long idx = evt.GetIndex();
    if (idx < 0 || idx >= (long)m_sessionRecords.size()) return;

    // Resolve the session file path from the project dir + filename stored in meta.
    std::string sessionFile = m_projectDir + "/" + m_sessionRecords[idx].sessionFile;
    if (!fs::exists(sessionFile)) {
        // sessionFile in meta may already be an absolute path.
        if (!fs::exists(m_sessionRecords[idx].sessionFile))
            return;
        sessionFile = m_sessionRecords[idx].sessionFile;
    }
    LoadQuestionList(sessionFile);
}

void ReviewPanel::OnQuestionActivated(wxListEvent& evt) {
    long idx = evt.GetIndex();
    if (idx < 0 || idx >= (long)m_questionRows.size()) return;
    const auto& qr = m_questionRows[idx];
    if (m_onDrill) m_onDrill(m_projectDir, qr.sessionFile, qr.questionIndex);
}

void ReviewPanel::OnQuestionRightClick(wxListEvent& evt) {
    long idx = evt.GetIndex();
    if (idx < 0 || idx >= (long)m_questionRows.size()) return;

    // Select the right-clicked row so OnToggleFlag knows which one.
    m_questionList->SetItemState(idx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

    bool currently = m_questionRows[idx].flagged;
    wxMenu menu;
    menu.Append(ID_TOGGLE_FLAG,
        currently ? "Remove flag" : "Flag for review");
    PopupMenu(&menu);
}

void ReviewPanel::OnToggleFlag(wxCommandEvent&) {
    long idx = m_questionList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (idx < 0 || idx >= (long)m_questionRows.size()) return;

    auto& qr = m_questionRows[idx];
    qr.flagged = !qr.flagged;
    SetTurnFlagged(qr.sessionFile, qr.questionIndex, qr.flagged);

    // Update the flag marker cell in place.
    m_questionList->SetItem(idx, 0,
        qr.flagged ? wxString::FromUTF8("⚑") : wxString(""));

    // Re-sync session list counts.
    LoadSessionList();
}
