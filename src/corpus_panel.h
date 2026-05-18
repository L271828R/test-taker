#pragma once
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
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
    void OnDelete();
    void OnOpen(wxListEvent& e);
    void RefreshList();
    void SetStatus(const std::string& msg);
    void ProcessFile(const std::string& filePath);

    std::string             m_projectDir;
    std::string             m_ollamaUrl;
    std::unique_ptr<Corpus> m_corpus;

    // Shared with background thread so it can detect panel teardown.
    std::shared_ptr<std::atomic<bool>> m_cancel;
    std::thread m_worker;

    wxListCtrl*   m_list    = nullptr;
    wxStaticText* m_status  = nullptr;
    wxButton*     m_addBtn  = nullptr;
    wxButton*     m_delBtn  = nullptr;
};
