#include "corpus_panel.h"
#include "embeddings.h"
#include "web_fetch.h"
#include <wx/clipbrd.h>
#include <wx/filedlg.h>
#include <wx/textdlg.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

CorpusPanel::CorpusPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    auto* top = new wxBoxSizer(wxVERTICAL);

    auto* hdr = new wxStaticText(this, wxID_ANY,
        "Add PDFs, text files, URLs, or clipboard text to build a searchable corpus. "
        "The exam automatically retrieves relevant passages when you start a session.");
    hdr->Wrap(700);
    top->Add(hdr, 0, wxALL, 8);

    m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SUNKEN);
    m_list->AppendColumn("Document",  wxLIST_FORMAT_LEFT,  340);
    m_list->AppendColumn("Chunks",    wxLIST_FORMAT_RIGHT,  70);
    m_list->AppendColumn("Added",     wxLIST_FORMAT_LEFT,  160);
    top->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* btnRow = new wxBoxSizer(wxHORIZONTAL);
    m_addBtn  = new wxButton(this, wxID_ANY, "Add File…");
    m_urlBtn  = new wxButton(this, wxID_ANY, "Add URL…");
    m_clipBtn = new wxButton(this, wxID_ANY, "Add Clipboard");
    m_delBtn  = new wxButton(this, wxID_ANY, "Delete");
    m_delBtn->Disable();
    btnRow->Add(m_addBtn,  0, wxRIGHT, 6);
    btnRow->Add(m_urlBtn,  0, wxRIGHT, 6);
    btnRow->Add(m_clipBtn, 0, wxRIGHT, 6);
    btnRow->Add(m_delBtn);
    top->Add(btnRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    m_status = new wxStaticText(this, wxID_ANY, "No project open.");
    top->Add(m_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    SetSizer(top);

    m_addBtn->Bind( wxEVT_BUTTON, [this](wxCommandEvent&){ OnAdd(); });
    m_urlBtn->Bind( wxEVT_BUTTON, [this](wxCommandEvent&){ OnAddURL(); });
    m_clipBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ OnAddClipboard(); });
    m_delBtn->Bind( wxEVT_BUTTON, [this](wxCommandEvent&){ OnDelete(); });

    m_list->Bind(wxEVT_LIST_ITEM_SELECTED,   [this](wxListEvent&){ m_delBtn->Enable(); });
    m_list->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&){ m_delBtn->Disable(); });
    m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED,  [this](wxListEvent& e){ OnOpen(e); });
}

CorpusPanel::~CorpusPanel() {
    *m_cancel = true;
    if (m_worker.joinable()) m_worker.detach();
}

void CorpusPanel::SyncProject(const std::string& projectDir, const std::string& ollamaUrl) {
    *m_cancel = true;
    if (m_worker.joinable()) m_worker.detach();
    m_cancel = std::make_shared<std::atomic<bool>>(false);

    m_projectDir = projectDir;
    m_ollamaUrl  = ollamaUrl;
    m_corpus.reset();
    m_list->DeleteAllItems();

    if (projectDir.empty()) {
        SetStatus("No project open.");
        m_addBtn->Disable();
        m_urlBtn->Disable();
        m_clipBtn->Disable();
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
    m_urlBtn->Enable();
    m_clipBtn->Enable();
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

// ---------------------------------------------------------------------------
void CorpusPanel::OnAdd() {
    if (!m_corpus) { SetStatus("Open a project first."); return; }

    wxFileDialog dlg(this, "Add to Corpus", "", "",
        "PDF files (*.pdf)|*.pdf|Text files (*.txt)|*.txt|All files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;

    ProcessFile(dlg.GetPath().ToUTF8().data());
}

// ---------------------------------------------------------------------------
void CorpusPanel::OnAddURL() {
    if (!m_corpus) { SetStatus("Open a project first."); return; }

    wxString entered = wxGetTextFromUser(
        "Enter the URL to download and add to the corpus.\n"
        "The page will be fetched with curl and its text extracted.",
        "Add URL", wxEmptyString, this).Trim();
    if (entered.empty()) return;

    std::string url = entered.ToStdString();

    // Derive filename from URL before fetching.
    std::string slug = NameFromURL(url);
    std::string name = slug + ".txt";

    // Check for duplicate name
    fs::path destPath = fs::path(m_projectDir) / "corpus" / name;
    if (fs::exists(destPath)) {
        wxMessageBox(wxString::FromUTF8("A corpus document named \"" + name +
                     "\" already exists. Delete it first if you want to re-fetch."),
                     "Duplicate", wxOK | wxICON_WARNING, this);
        return;
    }

    // Disable buttons and show progress while fetching.
    m_addBtn->Disable();
    m_urlBtn->Disable();
    m_clipBtn->Disable();
    SetStatus("Fetching " + url + " …");

    auto cancel     = m_cancel;
    std::string projectDir = m_projectDir;
    std::string ollamaUrl  = m_ollamaUrl;
    std::string dbPath     = projectDir + "/corpus.db";

    if (m_worker.joinable()) m_worker.detach();
    m_worker = std::thread([=]() {
        std::string fetchErr;
        std::string raw = FetchURL(url, fetchErr);

        if (raw.empty()) {
            if (!*cancel) wxTheApp->CallAfter([=](){
                if (!*cancel) {
                    SetStatus("Fetch failed: " + fetchErr);
                    m_addBtn->Enable(); m_urlBtn->Enable(); m_clipBtn->Enable();
                }
            });
            return;
        }

        // Strip HTML and check result. Show a preview dialog on curl failure
        // so the user can inspect what was returned.
        std::string text = StripHTML(raw);
        bool hadCurlError = !fetchErr.empty();

        if (hadCurlError || text.size() < 100) {
            // Surface the raw output for inspection — run on main thread.
            std::string rawCopy = raw;
            std::string statusMsg = hadCurlError
                ? "Fetch returned an error (curl: " + fetchErr + "). Showing raw response."
                : "Extracted text is very short. Showing raw response for inspection.";

            if (!*cancel) wxTheApp->CallAfter([=](){
                if (!*cancel) {
                    SetStatus(statusMsg);
                    m_addBtn->Enable(); m_urlBtn->Enable(); m_clipBtn->Enable();

                    // Show raw response in a scrollable dialog.
                    wxDialog dlg(nullptr, wxID_ANY, "Raw Response — " + url,
                                 wxDefaultPosition, wxSize(780, 500));
                    auto* sizer = new wxBoxSizer(wxVERTICAL);
                    auto* txt = new wxTextCtrl(&dlg, wxID_ANY,
                        wxString::FromUTF8(rawCopy),
                        wxDefaultPosition, wxDefaultSize,
                        wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
                    txt->SetFont(wxFont(11, wxFONTFAMILY_TELETYPE,
                                       wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
                    auto* note = new wxStaticText(&dlg, wxID_ANY,
                        "Review the raw response. If it looks correct, the extracted text "
                        "will still be added. If curl failed, the entry will not be saved.");
                    note->Wrap(740);
                    sizer->Add(note, 0, wxALL, 8);
                    sizer->Add(txt, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
                    auto* btns = dlg.CreateButtonSizer(wxOK);
                    sizer->Add(btns, 0, wxEXPAND | wxALL, 8);
                    dlg.SetSizer(sizer);
                    dlg.ShowModal();
                }
            });

            if (hadCurlError) return;  // Don't save on curl error
        }

        // Write text file to corpus dir.
        fs::path corpusDir = fs::path(projectDir) / "corpus";
        std::error_code ec;
        fs::create_directories(corpusDir, ec);
        fs::path dest = corpusDir / name;

        {
            std::ofstream f(dest.string());
            if (!f) {
                if (!*cancel) wxTheApp->CallAfter([=](){
                    if (!*cancel) {
                        SetStatus("Could not write file: " + dest.string());
                        m_addBtn->Enable(); m_urlBtn->Enable(); m_clipBtn->Enable();
                    }
                });
                return;
            }
            f << text;
        }

        // Add to DB and embed.
        Corpus bg(dbPath);
        std::string bgErr;
        if (!bg.Open(bgErr)) {
            if (!*cancel) wxTheApp->CallAfter([=](){
                if (!*cancel) {
                    SetStatus("DB error: " + bgErr);
                    m_addBtn->Enable(); m_urlBtn->Enable(); m_clipBtn->Enable();
                }
            });
            return;
        }

        int docId = bg.AddDocument(name, dest.string(), bgErr);
        if (docId < 0) {
            if (!*cancel) wxTheApp->CallAfter([=](){
                if (!*cancel) {
                    SetStatus("Failed to register: " + bgErr);
                    m_addBtn->Enable(); m_urlBtn->Enable(); m_clipBtn->Enable();
                }
            });
            return;
        }

        if (!*cancel) wxTheApp->CallAfter([=](){ if (!*cancel) { RefreshList(); } });

        auto chunks = ChunkText(text, 350, 50);
        int  total  = static_cast<int>(chunks.size());
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
                SetStatus("Done — " + std::to_string(total) + " chunks from " + url);
                m_addBtn->Enable(); m_urlBtn->Enable(); m_clipBtn->Enable();
            }
        });
    });
}

// ---------------------------------------------------------------------------
void CorpusPanel::OnAddClipboard() {
    if (!m_corpus) { SetStatus("Open a project first."); return; }

    // Read text from clipboard.
    std::string text;
    if (wxTheClipboard->Open()) {
        if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
            wxTextDataObject data;
            wxTheClipboard->GetData(data);
            text = data.GetText().ToStdString();
        }
        wxTheClipboard->Close();
    }

    if (text.empty()) {
        wxMessageBox("Clipboard is empty or contains no plain text.",
                     "Add Clipboard", wxOK | wxICON_INFORMATION, this);
        return;
    }

    wxString entered = wxGetTextFromUser(
        wxString::Format("Enter a name for this corpus entry (%zu chars from clipboard):",
                         text.size()),
        "Name for Clipboard Text", "clipboard", this).Trim();
    if (entered.empty()) return;

    std::string name = entered.ToStdString();
    // Sanitize: replace spaces and slashes with hyphens.
    for (char& c : name)
        if (c == ' ' || c == '/' || c == '\\') c = '-';
    if (name.size() > 60) name = name.substr(0, 60);
    if (name.find('.') == std::string::npos)
        name += ".txt";

    ProcessText(name, text);
}

// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
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
    m_urlBtn->Disable();
    m_clipBtn->Disable();

    auto cancel    = m_cancel;
    std::string projectDir = m_projectDir;
    std::string ollamaUrl  = m_ollamaUrl;
    std::string dbPath     = projectDir + "/corpus.db";

    if (m_worker.joinable()) m_worker.detach();
    m_worker = std::thread([=]() {
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
                            m_addBtn->Enable(); m_urlBtn->Enable(); m_clipBtn->Enable();
                        }
                    });
                    return;
                }
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
                    m_addBtn->Enable(); m_urlBtn->Enable(); m_clipBtn->Enable();
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
                m_addBtn->Enable(); m_urlBtn->Enable(); m_clipBtn->Enable();
            }
        });
    });
}

// ---------------------------------------------------------------------------
void CorpusPanel::ProcessText(const std::string& name, const std::string& text) {
    fs::path corpusDir = fs::path(m_projectDir) / "corpus";
    std::error_code ec;
    fs::create_directories(corpusDir, ec);
    fs::path dest = corpusDir / name;

    {
        std::ofstream f(dest.string());
        if (!f) { SetStatus("Could not write file: " + dest.string()); return; }
        f << text;
    }

    std::string err;
    int docId = m_corpus->AddDocument(name, dest.string(), err);
    if (docId < 0) { SetStatus("Failed to add: " + err); return; }

    RefreshList();
    SetStatus("Embedding…");
    m_addBtn->Disable();
    m_urlBtn->Disable();
    m_clipBtn->Disable();

    auto cancel    = m_cancel;
    std::string projectDir = m_projectDir;
    std::string ollamaUrl  = m_ollamaUrl;
    std::string dbPath     = projectDir + "/corpus.db";

    if (m_worker.joinable()) m_worker.detach();
    m_worker = std::thread([=]() {
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
                m_addBtn->Enable(); m_urlBtn->Enable(); m_clipBtn->Enable();
            }
        });
    });
}
