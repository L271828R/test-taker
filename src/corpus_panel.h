#pragma once
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "corpus.h"

class CorpusPanel : public wxPanel {
public:
    explicit CorpusPanel(wxWindow* parent);
    ~CorpusPanel();

    // Called by AppFrame when the active project changes.
    // ollamaUrl is used for the embedding pipeline and corpus search at exam time.
    void SyncProject(const std::string& projectDir,
                     const std::string& ollamaUrl = "http://localhost:11434");

private:
    void OnAdd();
    void OnAddURL();
    void OnAddClipboard();
    void OnAddGit();
    void OnDelete();
    void OnOpen(wxListEvent& e);
    void RefreshList();
    void SetStatus(const std::string& msg);
    void ProcessFile(const std::string& filePath);
    // Write text to <project>/corpus/<name>.txt and embed it.
    void ProcessText(const std::string& name, const std::string& text);

    std::string             m_projectDir;
    std::string             m_ollamaUrl;
    std::unique_ptr<Corpus> m_corpus;

    // Maps list row index → doc ids for that row (1 for standalone, N for groups).
    std::map<long, std::vector<int>> m_rowDocIds;

    // Shared with background thread so it can detect panel teardown.
    std::shared_ptr<std::atomic<bool>> m_cancel;
    std::thread m_worker;

    wxListCtrl*   m_list      = nullptr;
    wxStaticText* m_status    = nullptr;
    wxButton*     m_addBtn    = nullptr;
    wxButton*     m_urlBtn    = nullptr;
    wxButton*     m_clipBtn   = nullptr;
    wxButton*     m_gitBtn    = nullptr;
    wxButton*     m_delBtn    = nullptr;
};
