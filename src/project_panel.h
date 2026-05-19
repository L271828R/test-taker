#pragma once
#include <wx/panel.h>
#include <wx/treectrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/button.h>
#include <functional>
#include <set>
#include <string>

// ---------------------------------------------------------------------------
// Data attached to every node in the project tree.
// ---------------------------------------------------------------------------
struct TreeNode : public wxTreeItemData {
    enum class Kind { Folder, Project };
    Kind        kind;
    std::string path;   // absolute filesystem path
    std::string name;   // display name (folder or project name)
};

class ProjectPanel : public wxPanel {
public:
    using OpenCallback = std::function<void(const std::string&)>;

    ProjectPanel(wxWindow* parent, OpenCallback onProjectActivated);

    void RefreshProjects();

private:
    // ---- tree helpers -------------------------------------------------------
    // Populate children of parentId from dirPath up to depth levels.
    // query is the current search string (empty = show all).
    // Returns true when at least one project node was added (used for
    // filtering: a folder that contributes nothing is not added).
    bool PopulateTree(wxTreeItemId parentId,
                      const std::string& dirPath,
                      int depth,
                      const std::string& query,
                      int sortOrder);   // 0=Name 1=Created 2=Modified

    // ---- selection / activation ---------------------------------------------
    TreeNode* SelectedNode() const;
    void ActivateSelectedProject();

    // ---- event handlers -----------------------------------------------------
    void OnSearchChanged(wxCommandEvent& evt);
    void OnSortChanged(wxCommandEvent& evt);
    void OnActivateBtn(wxCommandEvent& evt);
    void OnRenameBtn(wxCommandEvent& evt);
    void OnNewProjectBtn(wxCommandEvent& evt);
    void OnNewSubfolder(wxCommandEvent& evt);
    void OnRefreshBtn(wxCommandEvent& evt);
    void OnSetFolderBtn(wxCommandEvent& evt);

    void OnDeleteBtn(wxCommandEvent& evt);
    void OnTreeSelChanged(wxTreeEvent& evt);
    void OnTreeItemActivated(wxTreeEvent& evt);
    void OnTreeExpanding(wxTreeEvent& evt);
    void OnTreeCollapsing(wxTreeEvent& evt);
    void OnTreeBeginDrag(wxTreeEvent& evt);
    void OnTreeEndDrag(wxTreeEvent& evt);

    // ---- widgets ------------------------------------------------------------
    wxTextCtrl*   m_searchCtrl;
    wxChoice*     m_sortChoice;
    wxTreeCtrl*   m_treeCtrl;
    wxStaticText* m_projectPathLabel;
    wxStaticText* m_statsLabel;
    wxButton*     m_activateBtn;
    wxButton*     m_renameBtn;
    wxButton*     m_deleteBtn;
    wxButton*     m_newProjectBtn;
    wxButton*     m_newSubfolderBtn;
    wxButton*     m_setFolderBtn;

    // ---- state --------------------------------------------------------------
    std::set<std::string> m_expandedPaths;   // paths whose folder nodes are expanded
    wxTreeItemId          m_dragItem;        // item being dragged
    OpenCallback          m_openCallback;
    bool                  m_startupDone = false; // auto-activate fires only once at startup

    wxDECLARE_EVENT_TABLE();
};
