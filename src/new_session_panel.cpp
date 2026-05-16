#include "new_session_panel.h"
#include "config.h"
#include "exam_meta.h"
#include "meta.h"
#include "llm_response.h"
#include "logger.h"
#include "project.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/config.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>

namespace fs = std::filesystem;

enum {
    ID_NS_START          = wxID_HIGHEST + 300,
    ID_NS_BACKEND,
    ID_NS_OPEN_CONTEXT,
};

wxBEGIN_EVENT_TABLE(NewSessionPanel, wxPanel)
    EVT_BUTTON(ID_NS_START,        NewSessionPanel::OnStart)
    EVT_CHOICE(ID_NS_BACKEND,      NewSessionPanel::OnBackendChanged)
    EVT_BUTTON(ID_NS_OPEN_CONTEXT, NewSessionPanel::OnOpenContext)
wxEND_EVENT_TABLE()

static wxArrayString make_difficulties() {
    wxArrayString s;
    for (auto* n : {"mixed", "easy", "medium", "hard"}) s.Add(n);
    return s;
}

static wxArrayString make_backends() {
    wxArrayString s;
    for (auto* n : {"claude -p", "Anthropic API", "Ollama (local)", "Clipboard (manual)"})
        s.Add(n);
    return s;
}

static std::vector<std::string> load_ollama_models() {
    FILE* pipe = popen("curl -s --max-time 1 http://localhost:11434/api/tags 2>/dev/null", "r");
    if (!pipe) return {};
    std::string out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    pclose(pipe);
    return ParseOllamaTags(out);
}

// ---------------------------------------------------------------------------
NewSessionPanel::NewSessionPanel(wxWindow* parent, StartCallback onSessionStarted)
    : wxPanel(parent), m_onStart(std::move(onSessionStarted))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* inner = new wxBoxSizer(wxVERTICAL);

    // ── Active project display ────────────────────────────────────────────
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, "Project:"),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        m_projectLabel = new wxStaticText(this, wxID_ANY, "(none — activate a project first)");
        wxFont f = m_projectLabel->GetFont();
        f.SetStyle(wxFONTSTYLE_ITALIC);
        m_projectLabel->SetFont(f);
        row->Add(m_projectLabel, 1, wxALIGN_CENTER_VERTICAL);
        inner->Add(row, 0, wxEXPAND | wxBOTTOM, 4);

        auto* ctxRow = new wxBoxSizer(wxHORIZONTAL);
        ctxRow->Add(new wxStaticText(this, wxID_ANY,
            "context.md (optional study material fed into every prompt):"),
            0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        ctxRow->Add(new wxButton(this, ID_NS_OPEN_CONTEXT, "Edit in vim",
            wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT),
            0, wxALIGN_CENTER_VERTICAL);
        inner->Add(ctxRow, 0, wxBOTTOM, 4);
    }
    inner->Add(new wxStaticLine(this), 0, wxEXPAND | wxBOTTOM, 10);

    // ── Topic ─────────────────────────────────────────────────────────────
    {
        auto* label = new wxStaticText(this, wxID_ANY, "What do you want to be tested on?");
        wxFont lf = label->GetFont();
        lf.SetPointSize(lf.GetPointSize() + 4);
        lf.SetWeight(wxFONTWEIGHT_BOLD);
        label->SetFont(lf);
        inner->Add(label, 0, wxBOTTOM, 6);

        m_topicCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
            wxDefaultPosition, wxSize(-1, 90),
            wxTE_MULTILINE | wxTE_RICH2 | wxTE_WORDWRAP);
        wxFont tf = m_topicCtrl->GetFont();
        tf.SetPointSize(tf.GetPointSize() + 2);
        m_topicCtrl->SetFont(tf);
        m_topicCtrl->SetHint(
            "e.g. \"AWS S3 and IAM policies\" or \"Python generators and coroutines\" "
            "or \"C++ move semantics\"…");
        inner->Add(m_topicCtrl, 0, wxEXPAND | wxBOTTOM, 10);
    }

    // ── Difficulty + Question count ───────────────────────────────────────
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);

        row->Add(new wxStaticText(this, wxID_ANY, "Difficulty:"),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        m_difficultyCtrl = new wxChoice(this, wxID_ANY, wxDefaultPosition,
                                        wxDefaultSize, make_difficulties());
        m_difficultyCtrl->SetSelection(0);
        row->Add(m_difficultyCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);

        row->Add(new wxStaticText(this, wxID_ANY, "Questions:"),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        m_countCtrl = new wxSpinCtrl(this, wxID_ANY, "10",
            wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, 50, 10);
        row->Add(m_countCtrl, 0, wxALIGN_CENTER_VERTICAL);

        inner->Add(row, 0, wxBOTTOM, 10);
    }
    inner->Add(new wxStaticLine(this), 0, wxEXPAND | wxBOTTOM, 10);

    // ── LLM backend ───────────────────────────────────────────────────────
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, "LLM backend:"),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        m_backendChoice = new wxChoice(this, ID_NS_BACKEND, wxDefaultPosition,
                                       wxDefaultSize, make_backends());
        m_backendChoice->SetSelection(0);
        row->Add(m_backendChoice, 0, wxALIGN_CENTER_VERTICAL);
        inner->Add(row, 0, wxBOTTOM, 6);

        // API key row (hidden unless Anthropic API selected)
        auto* apiRow = new wxBoxSizer(wxHORIZONTAL);
        apiRow->Add(new wxStaticText(this, wxID_ANY, "API key:"),
                    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        m_apiKeyCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
            wxDefaultPosition, wxSize(320, -1), wxTE_PASSWORD);
        apiRow->Add(m_apiKeyCtrl, 0, wxALIGN_CENTER_VERTICAL);
        m_apiKeySizer = inner->Add(apiRow, 0, wxBOTTOM, 6);
        m_apiKeySizer->Show(false);

        // Ollama model row (hidden unless Ollama selected)
        auto* ollamaRow = new wxBoxSizer(wxHORIZONTAL);
        ollamaRow->Add(new wxStaticText(this, wxID_ANY, "Ollama model:"),
                       0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        m_ollamaModel = new wxComboBox(this, wxID_ANY, "llama3",
            wxDefaultPosition, wxSize(240, -1), 0, nullptr, wxCB_DROPDOWN);
        ollamaRow->Add(m_ollamaModel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        auto* refreshBtn = new wxButton(this, wxID_ANY, "Refresh",
            wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        refreshBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            wxString cur = m_ollamaModel->GetValue();
            m_ollamaModel->Clear();
            for (auto& m : load_ollama_models())
                m_ollamaModel->Append(wxString::FromUTF8(m));
            m_ollamaModel->SetValue(cur.empty() && m_ollamaModel->GetCount() > 0
                ? m_ollamaModel->GetString(0) : cur);
            SetStatus(m_ollamaModel->GetCount() > 0
                ? "Loaded Ollama models." : "No Ollama models found.");
        });
        ollamaRow->Add(refreshBtn, 0, wxALIGN_CENTER_VERTICAL);
        m_ollamaSizer = inner->Add(ollamaRow, 0, wxBOTTOM, 8);
        m_ollamaSizer->Show(false);
    }
    inner->Add(new wxStaticLine(this), 0, wxEXPAND | wxBOTTOM, 10);

    // ── Start button ──────────────────────────────────────────────────────
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        m_startBtn = new wxButton(this, ID_NS_START, "Start Session");
        wxFont bf = m_startBtn->GetFont();
        bf.SetPointSize(bf.GetPointSize() + 2);
        bf.SetWeight(wxFONTWEIGHT_BOLD);
        m_startBtn->SetFont(bf);
        row->Add(m_startBtn, 0);
        inner->Add(row, 0, wxBOTTOM, 10);
    }

    // ── Status ────────────────────────────────────────────────────────────
    m_statusCtrl = new wxTextCtrl(this, wxID_ANY, "",
        wxDefaultPosition, wxSize(-1, 60),
        wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
    inner->Add(m_statusCtrl, 1, wxEXPAND);

    outer->Add(inner, 1, wxEXPAND | wxALL, 14);
    SetSizer(outer);

    RestoreFormState();
}

// ---------------------------------------------------------------------------
void NewSessionPanel::SyncProject(const std::string& projectDir) {
    m_activeProjectDir = projectDir;
    if (projectDir.empty()) {
        m_projectLabel->SetLabel("(none — activate a project first)");
    } else {
        m_projectLabel->SetLabel(wxString::FromUTF8(
            fs::path(projectDir).filename().string()));
    }
}

// ---------------------------------------------------------------------------
void NewSessionPanel::UpdateBackendFields() {
    std::string label = m_backendChoice->GetString(
        m_backendChoice->GetSelection()).ToStdString();
    m_apiKeySizer->Show(label == "Anthropic API");
    m_ollamaSizer->Show(label == "Ollama (local)");
    if (GetSizer()) GetSizer()->Layout();
}

// ---------------------------------------------------------------------------
void NewSessionPanel::SetStatus(const wxString& msg) {
    m_statusCtrl->SetValue(msg);
}

// ---------------------------------------------------------------------------
std::string NewSessionPanel::GenerateSessionFilename() const {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "session_%Y%m%d_%H%M%S.md", &tm);
    return buf;
}

// ---------------------------------------------------------------------------
void NewSessionPanel::SaveFormState() const {
    AppState st = LoadAppState();
    st.topic      = m_topicCtrl->GetValue().ToStdString();
    st.backend    = m_backendChoice->GetString(
        m_backendChoice->GetSelection()).ToStdString();
    st.apiKey     = m_apiKeyCtrl->GetValue().ToStdString();
    st.ollamaModel = m_ollamaModel->GetValue().ToStdString();
    SaveAppState(st);
}

void NewSessionPanel::RestoreFormState() {
    AppState st = LoadAppState();
    if (!st.topic.empty())
        m_topicCtrl->SetValue(wxString::FromUTF8(st.topic));
    if (!st.backend.empty()) {
        int idx = m_backendChoice->FindString(wxString::FromUTF8(st.backend));
        if (idx != wxNOT_FOUND) {
            m_backendChoice->SetSelection(idx);
            UpdateBackendFields();
        }
    }
    if (!st.apiKey.empty())
        m_apiKeyCtrl->SetValue(wxString::FromUTF8(st.apiKey));
    if (!st.ollamaModel.empty())
        m_ollamaModel->SetValue(wxString::FromUTF8(st.ollamaModel));
}

// ---------------------------------------------------------------------------
void NewSessionPanel::OnBackendChanged(wxCommandEvent&) {
    UpdateBackendFields();
    if (m_backendChoice->GetString(m_backendChoice->GetSelection()) == "Ollama (local)"
        && m_ollamaModel->GetCount() == 0) {
        for (auto& m : load_ollama_models())
            m_ollamaModel->Append(wxString::FromUTF8(m));
    }
}

// ---------------------------------------------------------------------------
void NewSessionPanel::OnOpenContext(wxCommandEvent&) {
    if (m_activeProjectDir.empty()) {
        wxMessageBox("Activate a project first.", "No project", wxOK | wxICON_WARNING, this);
        return;
    }
    std::string ctxPath = m_activeProjectDir + "/context.md";
    // Create stub if absent
    if (!fs::exists(ctxPath)) {
        std::ofstream f(ctxPath);
        f << "# Study Context\n\n"
             "Paste your study notes, syllabus, or reference material here.\n"
             "This file is injected into every exam prompt and chat message.\n";
    }
    wxExecute("open -a Terminal " + wxString::FromUTF8(
        "\"$(echo vim \\\"" + ctxPath + "\\\")\""), wxEXEC_ASYNC);
    // Simpler: just spawn vim directly in a terminal via the shell
    std::string cmd = "osascript -e 'tell application \"Terminal\" to do script "
                      "\"vim \\\"" + ctxPath + "\\\"\"'";
    wxExecute(wxString::FromUTF8(cmd), wxEXEC_ASYNC);
}

// ---------------------------------------------------------------------------
void NewSessionPanel::OnStart(wxCommandEvent&) {
    wxString topic = m_topicCtrl->GetValue().Trim();
    if (topic.empty()) {
        SetStatus("Enter a topic first.");
        return;
    }
    if (m_activeProjectDir.empty()) {
        SetStatus("Activate a project in the Projects tab first.");
        return;
    }

    // Build ExamConfig
    ExamConfig cfg;
    cfg.topic          = topic.ToStdString();
    cfg.difficulty     = m_difficultyCtrl->GetString(
        m_difficultyCtrl->GetSelection()).ToStdString();
    cfg.totalQuestions = m_countCtrl->GetValue();

    // Load context.md if present
    std::string ctxPath = m_activeProjectDir + "/context.md";
    std::ifstream ctxFile(ctxPath);
    if (ctxFile)
        cfg.projectContext.assign(std::istreambuf_iterator<char>(ctxFile), {});

    // Build LLMConfig
    std::string backendLabel = m_backendChoice->GetString(
        m_backendChoice->GetSelection()).ToStdString();
    LLMConfig llmCfg;
    llmCfg.backend     = BackendFromLabel(backendLabel);
    llmCfg.apiKey      = m_apiKeyCtrl->GetValue().ToStdString();
    llmCfg.ollamaModel = m_ollamaModel->GetValue().ToStdString();

    // Create session file
    if (!InitProject(m_activeProjectDir)) {
        SetStatus("Cannot initialise project folder.");
        return;
    }
    std::string sessionFile = m_activeProjectDir + "/" + GenerateSessionFilename();

    // Write session header
    {
        std::ofstream f(sessionFile);
        if (!f) { SetStatus("Cannot create session file."); return; }
        f << "# " << cfg.topic << " — Session\n\n"
          << "**Topic:** " << cfg.topic << "\n"
          << "**Difficulty:** " << cfg.difficulty << "\n"
          << "**Questions:** " << cfg.totalQuestions << "\n"
          << "**Backend:** " << backendLabel << "\n\n";
    }

    // Register session in metadata
    EnsureExamMeta(m_activeProjectDir, backendLabel);
    SessionRecord rec;
    rec.sessionFile    = fs::path(sessionFile).filename().string();
    rec.startedAt      = MetaNow();
    rec.topic          = cfg.topic;
    rec.difficulty     = cfg.difficulty;
    rec.totalQuestions = cfg.totalQuestions;
    RecordSession(m_activeProjectDir, rec);

    SaveFormState();

    Logger::get().log("Starting session: " + sessionFile
                      + "  topic=" + cfg.topic
                      + "  difficulty=" + cfg.difficulty
                      + "  questions=" + std::to_string(cfg.totalQuestions)
                      + "  backend=" + backendLabel);

    SetStatus("Starting: " + wxString::FromUTF8(cfg.topic));

    if (m_onStart) m_onStart(m_activeProjectDir, sessionFile, cfg, llmCfg);
}
