#include "corpus_panel.h"
#include "embeddings.h"
#include <wx/filedlg.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

CorpusPanel::CorpusPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    auto* top = new wxBoxSizer(wxVERTICAL);

    auto* hdr = new wxStaticText(this, wxID_ANY,
        "Upload PDF or text files to build a searchable corpus. "
        "The exam will automatically retrieve relevant passages when you start a session.");
    hdr->Wrap(700);
    top->Add(hdr, 0, wxALL, 8);

    m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SUNKEN);
    m_list->AppendColumn("Document",  wxLIST_FORMAT_LEFT,  340);
    m_list->AppendColumn("Chunks",    wxLIST_FORMAT_RIGHT,  70);
    m_list->AppendColumn("Added",     wxLIST_FORMAT_LEFT,  160);
    top->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* btnRow = new wxBoxSizer(wxHORIZONTAL);
    m_addBtn = new wxButton(this, wxID_ANY, "Add File…");
    m_delBtn = new wxButton(this, wxID_ANY, "Delete");
    m_delBtn->Disable();
    btnRow->Add(m_addBtn, 0, wxRIGHT, 6);
    btnRow->Add(m_delBtn);
    top->Add(btnRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    m_status = new wxStaticText(this, wxID_ANY, "No project open.");
    top->Add(m_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    SetSizer(top);

    m_addBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ OnAdd(); });
    m_delBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ OnDelete(); });

    m_list->Bind(wxEVT_LIST_ITEM_SELECTED,   [this](wxListEvent&){ m_delBtn->Enable(); });
    m_list->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&){ m_delBtn->Disable(); });
    m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED,  [this](wxListEvent& e){ OnOpen(e); });
}

CorpusPanel::~CorpusPanel() {
    *m_cancel = true;
    if (m_worker.joinable()) m_worker.detach();
}

void CorpusPanel::SyncProject(const std::string& projectDir, const std::string& ollamaUrl) {
    *m_cancel = true;                                                    // stop any running thread
    if (m_worker.joinable()) m_worker.detach();
    m_cancel = std::make_shared<std::atomic<bool>>(false);               // fresh flag for new project

    m_projectDir = projectDir;
    m_ollamaUrl  = ollamaUrl;
    m_corpus.reset();
    m_list->DeleteAllItems();

    if (projectDir.empty()) {
        SetStatus("No project open.");
        m_addBtn->Disable();
        return;
    }

    m_corpus = std::make_unique<Corpus>(projectDir + "/corpus.db");
    std::string err;
    if (!m_corpus->Open(err)) {
        SetStatus("Corpus DB error: " + err);
        m_corpus.reset();
        return;
    }
    m_addBtn->Enable();
    RefreshList();
    SetStatus("Ready.");
}

void CorpusPanel::RefreshList() {
    m_list->DeleteAllItems();
    m_delBtn->Disable();
    if (!m_corpus) return;

    for (const auto& doc : m_corpus->ListDocuments()) {
        long row = m_list->InsertItem(m_list->GetItemCount(), doc.name);
        m_list->SetItem(row, 1, std::to_string(m_corpus->ChunkCount(doc.id)));
        std::string date = doc.addedAt.size() >= 10 ? doc.addedAt.substr(0, 10) : doc.addedAt;
        m_list->SetItem(row, 2, date);
        m_list->SetItemData(row, static_cast<wxUIntPtr>(doc.id));
    }
}

void CorpusPanel::SetStatus(const std::string& msg) {
    m_status->SetLabel(msg);
}

void CorpusPanel::OnAdd() {
    if (!m_corpus) { SetStatus("Open a project first."); return; }

    wxFileDialog dlg(this, "Add to Corpus", "", "",
        "PDF files (*.pdf)|*.pdf|Text files (*.txt)|*.txt|All files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;

    ProcessFile(dlg.GetPath().ToUTF8().data());
}

void CorpusPanel::OnDelete() {
    long sel = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || !m_corpus) return;

    int docId = static_cast<int>(m_list->GetItemData(sel));
    std::string err;
    m_corpus->DeleteDocument(docId, err);
    RefreshList();
    SetStatus(err.empty() ? "Deleted." : "Delete failed: " + err);
}

void CorpusPanel::OnOpen(wxListEvent& e) {
    if (!m_corpus) return;
    int docId = static_cast<int>(m_list->GetItemData(e.GetIndex()));
    for (const auto& doc : m_corpus->ListDocuments()) {
        if (doc.id == docId) {
            wxLaunchDefaultApplication(wxString::FromUTF8(doc.path));
            return;
        }
    }
}

void CorpusPanel::ProcessFile(const std::string& filePath) {
    std::string err;
    std::string destPath = CopyFileToCorpusDir(m_projectDir, filePath, err);
    if (destPath.empty()) { SetStatus("Copy failed: " + err); return; }

    std::string name = fs::path(destPath).filename().string();
    int docId = m_corpus->AddDocument(name, destPath, err);
    if (docId < 0) { SetStatus("Failed to add: " + err); return; }

    RefreshList();
    SetStatus("Extracting text…");
    m_addBtn->Disable();

    auto cancel    = m_cancel;
    std::string projectDir = m_projectDir;
    std::string ollamaUrl  = m_ollamaUrl;
    std::string dbPath     = projectDir + "/corpus.db";

    if (m_worker.joinable()) m_worker.detach();
    m_worker = std::thread([=]() {
        // Read text from file
        std::string text;
        {
            std::string ext = fs::path(destPath).extension().string();
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            if (ext == ".pdf") {
                std::string pdfErr;
                text = ExtractPDF(destPath, pdfErr);
                if (text.empty()) {
                    if (!*cancel) wxTheApp->CallAfter([=](){
                        if (!*cancel) {
                            Corpus tmp(dbPath); std::string e;
                            if (tmp.Open(e)) tmp.DeleteDocument(docId, e);
                            RefreshList();
                            SetStatus("PDF extraction failed (is pdftotext installed?): " + pdfErr);
                            m_addBtn->Enable();
                        }
                    });
                    return;
                }
                // Save sidecar alongside the copied PDF in the corpus dir
                std::ofstream sf(destPath + ".txt");
                sf << text;
            } else {
                std::ifstream f(destPath);
                text.assign(std::istreambuf_iterator<char>(f), {});
            }
        }

        if (text.empty()) {
            if (!*cancel) wxTheApp->CallAfter([=](){
                if (!*cancel) {
                    SetStatus("File is empty — nothing to embed.");
                    m_addBtn->Enable();
                }
            });
            return;
        }

        auto chunks = ChunkText(text, 350, 50);
        int  total  = static_cast<int>(chunks.size());

        Corpus bg(dbPath);
        std::string bgErr;
        if (!bg.Open(bgErr)) return;

        for (int i = 0; i < total && !*cancel; ++i) {
            if (!IsUsefulChunk(chunks[i])) continue;
            auto emb = EmbedText(chunks[i], ollamaUrl);
            if (emb.ok) bg.AddChunk(docId, i, chunks[i], emb.embedding, bgErr);

            int done = i + 1;
            if (!*cancel) wxTheApp->CallAfter([=](){
                if (!*cancel)
                    SetStatus("Embedding " + std::to_string(done) + "/" +
                               std::to_string(total) + "…");
            });
        }

        if (!*cancel) wxTheApp->CallAfter([=](){
            if (!*cancel) {
                RefreshList();
                SetStatus("Done — " + std::to_string(total) + " chunks embedded.");
                m_addBtn->Enable();
            }
        });
    });
}
