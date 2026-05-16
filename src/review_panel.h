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
    wxListCtrl*   m_flaggedList  = nullptr;
    wxStaticText* m_summaryLabel = nullptr;

    struct FlaggedRow {
        std::string sessionFile;
        int         questionIndex = 0;
        std::string questionText;
        Score       previousScore = Score::Skipped;
    };
    std::vector<FlaggedRow> m_flaggedRows;

    void LoadSessionList();
    void LoadFlaggedList();

    void OnFlaggedActivated(wxListEvent&);

    wxDECLARE_EVENT_TABLE();
};
