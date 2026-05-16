#pragma once
#include <functional>
#include <string>
#include <vector>
#include <wx/wx.h>
#include <wx/checklst.h>

// Panel for browsing chapters and tidbits and rewriting them via an LLM.
class EditPanel : public wxPanel {
public:
    using OpenCallback = std::function<void(const std::string& filepath)>;
    EditPanel(wxWindow* parent, OpenCallback onFileChanged);

    // Call after a new chapter is generated so the chapter list refreshes.
    void RefreshChapters();

private:
    OpenCallback      m_openCallback;

    // ── Content navigation ────────────────────────────────────────────────
    wxListBox*        m_chapterList;
    wxButton*         m_moveUpBtn;
    wxButton*         m_moveDownBtn;
    wxButton*         m_renameFileBtn;
    wxButton*         m_deleteFileBtn;
    wxStaticText*     m_rightLabel;
    wxListBox*        m_tidbitList;
    wxRadioButton*    m_radioOpenView;
    wxRadioButton*    m_radioOpenVim;
    wxRadioButton*    m_radioTidbit;
    wxRadioButton*    m_radioChapter;

    // ── Rewrite ───────────────────────────────────────────────────────────
    wxTextCtrl*       m_instructCtrl;
    wxButton*         m_rewriteBtn;
    wxTextCtrl*       m_translateLangCtrl;
    wxButton*         m_translateBtn;

    // ── Git version history ───────────────────────────────────────────────
    wxListBox*        m_historyList;   // git log of selected file
    wxTextCtrl*       m_commitMsgCtrl;
    wxButton*         m_commitBtn;
    wxButton*         m_viewVerBtn;
    wxButton*         m_diffBtn;
    wxButton*         m_restoreBtn;
    wxButton*         m_checkoutBtn;
    wxButton*         m_stashBtn;
    wxButton*         m_unstashBtn;

    // ── Status ────────────────────────────────────────────────────────────
    wxTextCtrl*       m_statusCtrl;

    struct TidbitEntry  { int id; std::string preview; };
    struct SectionEntry { int id; std::string preview; };
    std::vector<TidbitEntry>  m_tidbits;
    std::vector<SectionEntry> m_sections;

    // git log parallel to m_historyList rows
    struct CommitEntry { std::string hash; std::string shortHash;
                         std::string date;  std::string subject; };
    std::vector<CommitEntry> m_commits;

    std::string CurrentProjectPath() const;
    std::string CurrentChapterPath() const;
    void        LoadTidbits();
    void        LoadSections();
    void        ReloadRightList();
    void        LoadHistory();
    void        OpenCurrentFileInVim();
    void        SaveCurrentFileOrder() const;

    void OnRefresh(wxCommandEvent&);
    void OnChapterSelected(wxCommandEvent&);
    void OnChapterActivated(wxCommandEvent&);
    void OnMoveFileUp(wxCommandEvent&);
    void OnMoveFileDown(wxCommandEvent&);
    void OnRenameFile(wxCommandEvent&);
    void OnDeleteFile(wxCommandEvent&);
    void OnTargetChanged(wxCommandEvent&);
    void OnRewrite(wxCommandEvent&);
    void OnTranslate(wxCommandEvent&);
    void OnCommit(wxCommandEvent&);
    void OnViewVersion(wxCommandEvent&);
    void OnDiff(wxCommandEvent&);
    void OnRestore(wxCommandEvent&);
    void OnCheckout(wxCommandEvent&);
    void OnStash(wxCommandEvent&);
    void OnUnstash(wxCommandEvent&);
    void SetStatus(const wxString& msg);
    void SetBusy(bool on);

    wxDECLARE_EVENT_TABLE();
};
