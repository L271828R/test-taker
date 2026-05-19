#include "new_session_panel.h"
#include "focus_list_panel.h"
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
#include <sstream>
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

// ---------------------------------------------------------------------------
// Personality persistence helpers
// Pipe-separated names, e.g. "Albert Einstein|Bill Gates"
// ---------------------------------------------------------------------------
static std::string JoinPipe(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) {
        if (!out.empty()) out += "|";
        out += s;
    }
    return out;
}

static std::vector<std::string> SplitPipe(const std::string& s) {
    std::vector<std::string> result;
    if (s.empty()) return result;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, '|'))
        if (!tok.empty()) result.push_back(tok);
    return result;
}

// ---------------------------------------------------------------------------
// Focus-area list persistence helpers
// Format: "stars@@text|stars@@text|..."
// ---------------------------------------------------------------------------
static std::string SerializeFocusAreas(const std::vector<FocusArea>& areas) {
    std::string out;
    for (const auto& a : areas) {
        if (!out.empty()) out += "|";
        out += std::to_string(a.stars) + "@@" + a.text;
    }
    return out;
}

static std::vector<FocusArea> DeserializeFocusAreas(const std::string& s) {
    std::vector<FocusArea> result;
    if (s.empty()) return result;
    std::string item;
    std::istringstream ss(s);
    while (std::getline(ss, item, '|')) {
        auto sep = item.find("@@");
        if (sep == std::string::npos) continue;
        int stars = std::stoi(item.substr(0, sep));
        std::string text = item.substr(sep + 2);
        if (!text.empty())
            result.push_back({text, std::max(1, std::min(5, stars))});
    }
    return result;
}

enum {
    ID_NS_START          = wxID_HIGHEST + 300,
    ID_NS_BACKEND,
    ID_NS_OPEN_CONTEXT,
    ID_NS_RESET_WEIGHTS,
};

wxBEGIN_EVENT_TABLE(NewSessionPanel, wxPanel)
    EVT_BUTTON(ID_NS_START,          NewSessionPanel::OnStart)
    EVT_CHOICE(ID_NS_BACKEND,        NewSessionPanel::OnBackendChanged)
    EVT_BUTTON(ID_NS_OPEN_CONTEXT,   NewSessionPanel::OnOpenContext)
    EVT_BUTTON(ID_NS_RESET_WEIGHTS,  NewSessionPanel::OnResetWeights)
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

    // ── Topic label ───────────────────────────────────────────────────────
    {
        auto* label = new wxStaticText(this, wxID_ANY, "Topic (short name for Review tab):");
        wxFont lf = label->GetFont();
        lf.SetWeight(wxFONTWEIGHT_BOLD);
        label->SetFont(lf);
        inner->Add(label, 0, wxBOTTOM, 4);

        m_topicCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
            wxDefaultPosition, wxSize(-1, -1), wxTE_RICH2);
        wxFont tf = m_topicCtrl->GetFont();
        tf.SetPointSize(tf.GetPointSize() + 2);
        m_topicCtrl->SetFont(tf);
        m_topicCtrl->SetHint("e.g. \"C++ memory model\" or \"AWS IAM\" or \"Spanish verbs\"");
        inner->Add(m_topicCtrl, 0, wxEXPAND | wxBOTTOM, 10);
    }

    // ── Instructions ─────────────────────────────────────────────────────
    {
        auto* label = new wxStaticText(this, wxID_ANY,
            "What to focus on (injected into every prompt — be specific):");
        inner->Add(label, 0, wxBOTTOM, 4);

        m_instrCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
            wxDefaultPosition, wxSize(-1, 80),
            wxTE_MULTILINE | wxTE_RICH2 | wxTE_WORDWRAP);
        m_instrCtrl->SetHint(
            "e.g. \"Focus on move semantics, RAII, and smart pointers. "
            "Avoid basic syntax questions.\"");
        inner->Add(m_instrCtrl, 0, wxEXPAND | wxBOTTOM, 10);
    }

    // ── Focus areas ──────────────────────────────────────────────────────
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY,
            "Focus areas — add sub-topics and rate priority (★ = low, ★★★★★ = high):"),
            1, wxALIGN_CENTER_VERTICAL);
        auto* resetBtn = new wxButton(this, ID_NS_RESET_WEIGHTS,
            wxString::FromUTF8("\xe2\x86\xba Reset \xf0\x9f\x91\x8d\xf0\x9f\x91\x8e weights"),
            wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        resetBtn->SetToolTip("Clear all thumbs-up / thumbs-down topic preferences saved from exams");
        row->Add(resetBtn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 8);
        inner->Add(row, 0, wxEXPAND | wxBOTTOM, 4);

        m_focusListPanel = new FocusListPanel(this, 140);
        inner->Add(m_focusListPanel, 0, wxEXPAND | wxBOTTOM, 10);
    }

    // ── Personality picker ────────────────────────────────────────────────
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY,
            "Guest commentators — checked personalities appear as \xf0\x9f\x92\xac tidbits in explanations:"),
            1, wxALIGN_CENTER_VERTICAL);
        row->Add(new wxStaticText(this, wxID_ANY, "Tidbits per turn:"),
            0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
        m_tidbitCountCtrl = new wxSpinCtrl(this, wxID_ANY, "1",
            wxDefaultPosition, wxSize(52, -1), wxSP_ARROW_KEYS, 1, 10, 1);
        m_tidbitCountCtrl->SetToolTip("How many tidbit commentaries to include per explanation (1–10)");
        row->Add(m_tidbitCountCtrl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
        inner->Add(row, 0, wxEXPAND | wxBOTTOM, 4);

        m_personalityPanel = new PersonalityPickerPanel(this);
        inner->Add(m_personalityPanel, 0, wxEXPAND | wxBOTTOM, 10);
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

    // ── Corpus toggle (shown only when corpus.db exists for the project) ──
    {
        m_useCorpusCheck = new wxCheckBox(this, wxID_ANY,
            "Use corpus for context (RAG) — retrieve relevant passages before each session");
        m_corpusSizer = inner->Add(m_useCorpusCheck, 0, wxBOTTOM, 10);
        m_corpusSizer->Show(false);
    }

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
    // On first call after a fresh launch m_activeProjectDir is empty, but the
    // user may be reopening the same project they had last time.  Seed from
    // the persisted value so we can detect a true project change.
    if (m_activeProjectDir.empty()) {
        AppState saved = LoadAppState();
        m_activeProjectDir = saved.lastExamProjectDir;
    }
    bool projectChanged = (projectDir != m_activeProjectDir);
    m_activeProjectDir = projectDir;

    if (projectDir.empty()) {
        m_projectLabel->SetLabel("(none — activate a project first)");
        m_corpusSizer->Show(false);
    } else {
        m_projectLabel->SetLabel(wxString::FromUTF8(
            fs::path(projectDir).filename().string()));

        bool hasCorpus = fs::exists(projectDir + "/corpus.db");
        m_corpusSizer->Show(hasCorpus);
        if (hasCorpus) m_useCorpusCheck->SetValue(true);

        if (projectChanged) {
            // Different project: start with blank topic and instructions so that
            // session-specific state from the previous project (e.g. C++ focus) doesn't
            // bleed in.  Only restore the backend/credentials which are truly global.
            m_topicCtrl->Clear();
            m_instrCtrl->Clear();
            m_focusListPanel->SetAreas({});
            // Load per-project personality selection from the project's .config file.
            ProjectConfig pcfg = LoadConfig(projectDir);
            m_personalityPanel->SetSelected(SplitPipe(pcfg.personalities));
            AppState st = LoadAppState();
            if (!st.backend.empty()) {
                int idx = m_backendChoice->FindString(wxString::FromUTF8(st.backend));
                if (idx != wxNOT_FOUND) {
                    m_backendChoice->SetSelection(idx);
                    UpdateBackendFields();
                }
            }
            if (!st.apiKey.empty())     m_apiKeyCtrl->SetValue(wxString::FromUTF8(st.apiKey));
            if (!st.ollamaModel.empty()) m_ollamaModel->SetValue(wxString::FromUTF8(st.ollamaModel));
        } else {
            RestoreFormState();
            // Reload personalities from project config (same project, may have changed).
            ProjectConfig pcfg = LoadConfig(projectDir);
            m_personalityPanel->SetSelected(SplitPipe(pcfg.personalities));
        }
    }
    if (GetSizer()) GetSizer()->Layout();
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
    // All exam form state is per-project so two instances on different projects
    // never clobber each other.
    if (!m_activeProjectDir.empty()) {
        ProjectConfig pcfg = LoadConfig(m_activeProjectDir);
        pcfg.examTopic        = m_topicCtrl->GetValue().ToStdString();
        pcfg.examInstructions = m_instrCtrl->GetValue().ToStdString();
        pcfg.examFocusAreas   = SerializeFocusAreas(m_focusListPanel->GetAreas());
        pcfg.examBackend      = m_backendChoice->GetString(
            m_backendChoice->GetSelection()).ToStdString();
        pcfg.examApiKey       = m_apiKeyCtrl->GetValue().ToStdString();
        pcfg.examOllamaModel  = m_ollamaModel->GetValue().ToStdString();
        pcfg.personalities    = JoinPipe(m_personalityPanel->GetSelected());
        pcfg.examTidbitCount  = m_tidbitCountCtrl->GetValue();
        SaveConfig(m_activeProjectDir, pcfg);
    }

    // Only the active project dir stays in global state so we know which project
    // to re-open on next startup.
    AppState st = LoadAppState();
    st.lastExamProjectDir = m_activeProjectDir;
    SaveAppState(st);
}

void NewSessionPanel::RestoreFormState() {
    if (m_activeProjectDir.empty()) return;
    ProjectConfig pcfg = LoadConfig(m_activeProjectDir);
    if (!pcfg.examTopic.empty())
        m_topicCtrl->SetValue(wxString::FromUTF8(pcfg.examTopic));
    if (!pcfg.examInstructions.empty())
        m_instrCtrl->SetValue(wxString::FromUTF8(pcfg.examInstructions));
    if (!pcfg.examBackend.empty()) {
        int idx = m_backendChoice->FindString(wxString::FromUTF8(pcfg.examBackend));
        if (idx != wxNOT_FOUND) {
            m_backendChoice->SetSelection(idx);
            UpdateBackendFields();
        }
    }
    if (!pcfg.examFocusAreas.empty())
        m_focusListPanel->SetAreas(DeserializeFocusAreas(pcfg.examFocusAreas));
    if (pcfg.examTidbitCount >= 1 && pcfg.examTidbitCount <= 10)
        m_tidbitCountCtrl->SetValue(pcfg.examTidbitCount);
    if (!pcfg.examApiKey.empty())
        m_apiKeyCtrl->SetValue(wxString::FromUTF8(pcfg.examApiKey));
    if (!pcfg.examOllamaModel.empty())
        m_ollamaModel->SetValue(wxString::FromUTF8(pcfg.examOllamaModel));
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
    cfg.instructions   = m_instrCtrl->GetValue().Trim().ToStdString();
    cfg.focusAreaList  = m_focusListPanel->GetAreas();
    cfg.difficulty     = m_difficultyCtrl->GetString(
        m_difficultyCtrl->GetSelection()).ToStdString();
    cfg.totalQuestions = m_countCtrl->GetValue();
    cfg.useCorpus      = m_corpusSizer->IsShown() && m_useCorpusCheck->GetValue();
    cfg.personalities  = m_personalityPanel->GetSelected();
    cfg.tidbitCount    = m_tidbitCountCtrl->GetValue();

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

    cfg.largeModel = IsLargeModel(llmCfg.backend);

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
          << "**Topic:** " << cfg.topic << "\n";
        if (!cfg.instructions.empty())
            f << "**Instructions:** " << cfg.instructions << "\n";
        f << "**Difficulty:** " << cfg.difficulty << "\n"
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

    // Persist last session per-project so it can be resumed after restart.
    {
        ProjectConfig pcfg = LoadConfig(m_activeProjectDir);
        pcfg.lastSession = fs::path(sessionFile).filename().string();
        SaveConfig(m_activeProjectDir, pcfg);
    }

    Logger::get().log("Starting session: " + sessionFile
                      + "  topic=" + cfg.topic
                      + "  focusAreas=" + std::to_string(cfg.focusAreaList.size())
                      + "  difficulty=" + cfg.difficulty
                      + "  questions=" + std::to_string(cfg.totalQuestions)
                      + "  backend=" + backendLabel);

    SetStatus("Starting: " + wxString::FromUTF8(cfg.topic));

    if (m_onStart) m_onStart(m_activeProjectDir, sessionFile, cfg, llmCfg);
}

// ---------------------------------------------------------------------------
void NewSessionPanel::OnResetWeights(wxCommandEvent&) {
    if (m_activeProjectDir.empty()) return;
    ProjectConfig pcfg = LoadConfig(m_activeProjectDir);
    pcfg.examMoreOf.clear();
    pcfg.examLessOf.clear();
    SaveConfig(m_activeProjectDir, pcfg);
    SetStatus("Topic weights cleared.");
}

// ---------------------------------------------------------------------------
void NewSessionPanel::PreFill(const std::string&           topic,
                               const std::vector<FocusArea>& focusAreas,
                               const std::string&           difficulty,
                               int                          questionCount) {
    if (!topic.empty())
        m_topicCtrl->SetValue(wxString::FromUTF8(topic));
    m_focusListPanel->SetAreas(focusAreas);

    if (!difficulty.empty()) {
        int idx = m_difficultyCtrl->FindString(wxString::FromUTF8(difficulty));
        if (idx != wxNOT_FOUND) m_difficultyCtrl->SetSelection(idx);
    }
    if (questionCount > 0) m_countCtrl->SetValue(questionCount);
}
