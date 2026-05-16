#pragma once
#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/webview.h>
#include <wx/filename.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <string>
#include "logger.h"
#include "edit_panel.h"
#include "create_panel.h"
#include "project_panel.h"
#include "chat_frame.h"

enum {
    ID_RELOAD       = wxID_HIGHEST + 1,
    ID_THEME_LIGHT,
    ID_THEME_DARK,
    ID_VIEW_LOGS,
    ID_VIEW_DOC,
    ID_FONT_INCREASE,
    ID_FONT_DECREASE,
    ID_FONT_RESET,
    ID_FIND_NEXT,
    ID_FIND_PREV,
    ID_FIND_CLOSE,
    ID_SAVE_HTML,
    ID_CHAT_OPEN,
};

class MDViewerFrame : public wxFrame {
public:
    explicit MDViewerFrame(const wxString& filePath);
    void LoadFile(const std::string& path);

private:
    wxNotebook*   m_notebook    = nullptr;
    wxPanel*      m_viewPage    = nullptr;
    EditPanel*    m_editPage    = nullptr;
    CreatePanel*  m_createPage  = nullptr;
    ProjectPanel* m_projectPage = nullptr;
    wxWebView*    m_webView;
    wxString      m_filePath;
    bool          m_darkMode;
    int           m_fontSizePercent;
    wxPanel*      m_findPanel   = nullptr;
    wxTextCtrl*   m_findCtrl    = nullptr;
    wxStaticText* m_findStatus  = nullptr;
    wxString      m_findTerm;
    int           m_findTotal   = 0;
    int           m_findCurrent = 0;
    ChatFrame*    m_chatFrame   = nullptr;

    void LoadAndRender();
    std::string ReadFile(const std::string& path);
    void ShowFindBar(bool show);
    void PositionFindBar();
    void DoFind(bool forward);

    void OnNoteAdd(const std::string& selectedText, const std::string& context);
    void OnNoteEdit(int noteId);
    void OnNoteDelete(int noteId);
    std::string CurrentProjectDir() const;

    void OnOpen(wxCommandEvent& evt);
    void OnReload(wxCommandEvent& evt);
    void OnThemeLight(wxCommandEvent& evt);
    void OnThemeDark(wxCommandEvent& evt);
    void OnViewLogs(wxCommandEvent& evt);
    void OnViewDoc(wxCommandEvent& evt);
    void OnFontIncrease(wxCommandEvent& evt);
    void OnFontDecrease(wxCommandEvent& evt);
    void OnFontReset(wxCommandEvent& evt);
    void OnScriptMessage(wxWebViewEvent& evt);
    void OnExit(wxCommandEvent& evt);
    void OnClose(wxCloseEvent& evt);
    void OnCopy(wxCommandEvent& evt);
    void OnSelectAll(wxCommandEvent& evt);
    void OnPasteView(wxCommandEvent& evt);
    void OnFindOpen(wxCommandEvent& evt);
    void OnFindNext(wxCommandEvent& evt);
    void OnFindPrev(wxCommandEvent& evt);
    void OnFindClose(wxCommandEvent& evt);
    void OnSaveHTML(wxCommandEvent& evt);
    void OpenChat(int chId, const std::string& chTitle);

    wxDECLARE_EVENT_TABLE();
};
