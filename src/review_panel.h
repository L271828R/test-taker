#pragma once
#include <functional>
#include <string>
#include <wx/wx.h>
#include <wx/listctrl.h>
#include "session.h"
#include "exam_meta.h"

class ReviewPanel : public wxPanel {
public:
    using DrillCallback = std::function<void(const std::string& projectDir,
                                             const std::string& sessionFile,
                                             int                questionIndex)>;

    ReviewPanel(wxWindow* parent, DrillCallback onDrillRequested);

    void RefreshSessions(const std::string& projectDir);

private:
    DrillCallback m_onDrill;
    std::string   m_projectDir;

    wxListCtrl*   m_sessionList  = nullptr;
    wxListCtrl*   m_questionList = nullptr;
    wxStaticText* m_summaryLabel = nullptr;

    struct QuestionRow {
        std::string sessionFile;
        int         questionIndex = 0;
        std::string questionText;
        Score       score         = Score::Skipped;
        bool        flagged       = false;
    };
    std::vector<QuestionRow> m_questionRows;

    // sessions parallel to m_sessionList rows (newest-first order)
    std::vector<SessionRecord> m_sessionRecords;

    void LoadSessionList();
    void LoadQuestionList(const std::string& sessionFile);

    void OnSessionSelected(wxListEvent&);
    void OnQuestionActivated(wxListEvent&);
    void OnQuestionRightClick(wxListEvent&);
    void OnToggleFlag(wxCommandEvent&);

    wxDECLARE_EVENT_TABLE();
};
