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
#include <sstream>
#include <wx/config.h>
#include <wx/msgdlg.h>
#include <wx/tokenzr.h>
#include <wx/sizer.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Small JSON field extractor (matches pattern used in other panels)
// ---------------------------------------------------------------------------
static std::string nsJsonField(const std::string& json, const std::string& key) {
    // Handles both numeric and string values: "key":"value" or "key":123
    std::string kq = "\"" + key + "\":";
    auto pos = json.find(kq);
    if (pos == std::string::npos) return "";
    pos += kq.size();
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // String value
        ++pos;
        std::string val;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos;
                switch (json[pos]) {
                    case 'n': val += '\n'; break;
                    case 't': val += '\t'; break;
                    case '"': val += '"'; break;
                    case '\\': val += '\\'; break;
                    default: val += json[pos]; break;
                }
            } else {
                val += json[pos];
            }
            ++pos;
        }
        return val;
    } else {
        // Numeric/boolean value — read until delimiter
        size_t end = json.find_first_of(",}", pos);
        if (end == std::string::npos) end = json.size();
        return json.substr(pos, end - pos);
    }
}

// Pipe-join / split helpers
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
// Fetch Ollama model list from localhost
// ---------------------------------------------------------------------------
static std::vector<std::string> FetchOllamaModels() {
    FILE* pipe = popen("curl -s --max-time 1 http://localhost:11434/api/tags 2>/dev/null", "r");
    if (!pipe) return {};
    std::string out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    pclose(pipe);
    return ParseOllamaTags(out);
}

// ---------------------------------------------------------------------------
// Event table
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

NewSessionPanel::NewSessionPanel(wxWindow* parent, bool darkMode, StartCallback onSessionStarted)
    : wxPanel(parent)
    , m_onStart(std::move(onSessionStarted))
    , m_darkMode(darkMode)
{
    m_state.darkMode = darkMode;

    m_webView = wxWebView::New(this, wxID_ANY);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_webView, 1, wxEXPAND);
    SetSizer(sizer);

    // wxWebViewEvent does not propagate — must bind on the webview itself.
    m_webView->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
                    &NewSessionPanel::OnNsAction, this);

    // Stub page so the webview has something to show before Render()
    m_webView->SetPage("<html><body></body></html>", "");

    // AddScriptMessageHandler internally calls RunScript which pumps the event
    // loop — defer until after construction is fully complete.
    CallAfter([this]() {
        m_webView->AddScriptMessageHandler("nsAction");
    });

    LoadPersonalityLibrary();
    Render();
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void NewSessionPanel::Render() {
    m_state.darkMode = m_darkMode;
    std::string html = BuildNewSessionHTML(m_state);
    m_webView->SetPage(wxString::FromUTF8(html), "");
}

// ---------------------------------------------------------------------------
// SetDarkMode
// ---------------------------------------------------------------------------

void NewSessionPanel::SetDarkMode(bool dark) {
    m_darkMode = dark;
    Render();
}

// ---------------------------------------------------------------------------
void NewSessionPanel::ReloadLibrary() {
    LoadPersonalityLibrary();
    Render();
}

// ---------------------------------------------------------------------------
// LoadPersonalityLibrary — reads checked set from the shared Personas tab config
// ---------------------------------------------------------------------------

void NewSessionPanel::LoadPersonalityLibrary() {
    wxConfig cfg("TestTaker");
    cfg.SetPath("/charlib");
    wxString checkedStr;
    cfg.Read("checked", &checkedStr);
    m_state.selectedPersonalities = SplitPipe(checkedStr.ToStdString());
}

// ---------------------------------------------------------------------------
// SyncProject
// ---------------------------------------------------------------------------

void NewSessionPanel::SyncProject(const std::string& projectDir) {
    if (m_activeProjectDir.empty()) {
        AppState saved = LoadAppState();
        m_activeProjectDir = saved.lastExamProjectDir;
    }
    bool projectChanged = (projectDir != m_activeProjectDir);
    m_activeProjectDir  = projectDir;

    if (projectDir.empty()) {
        m_state.projectName = "";
        m_state.hasCorpus   = false;
    } else {
        m_state.projectName = fs::path(projectDir).filename().string();
        m_state.hasCorpus   = fs::exists(projectDir + "/corpus.db");
        if (m_state.hasCorpus) m_state.useCorpus = true;

        ProjectConfig pcfg = LoadConfig(projectDir);

        if (projectChanged) {
            // New project — clear session-specific state, keep backend/credentials
            m_state.topic        = "";
            m_state.instructions = "";
            m_state.focusAreas   = {};

            AppState st = LoadAppState();
            if (!st.backend.empty())    m_state.backend    = st.backend;
            if (!st.apiKey.empty())     m_state.apiKey     = st.apiKey;
            if (!st.ollamaModel.empty()) m_state.ollamaModel = st.ollamaModel;
        } else {
            // Same project — restore saved form state
            if (!pcfg.examTopic.empty())        m_state.topic        = pcfg.examTopic;
            if (!pcfg.examInstructions.empty())  m_state.instructions = pcfg.examInstructions;
            if (!pcfg.examFocusAreas.empty())
                m_state.focusAreas = DeserializeFocusAreas(pcfg.examFocusAreas);
            if (!pcfg.examBackend.empty())      m_state.backend      = pcfg.examBackend;
            if (!pcfg.examApiKey.empty())        m_state.apiKey       = pcfg.examApiKey;
            if (!pcfg.examOllamaModel.empty())   m_state.ollamaModel  = pcfg.examOllamaModel;
            if (pcfg.examTidbitCount >= 1 && pcfg.examTidbitCount <= 10)
                m_state.tidbitCount = pcfg.examTidbitCount;
        }
    }

    Render();
}

// ---------------------------------------------------------------------------
// PreFill — called from deep-dive dialog
// ---------------------------------------------------------------------------

void NewSessionPanel::PreFill(const std::string&           topic,
                               const std::vector<FocusArea>& focusAreas,
                               const std::string&           difficulty,
                               int                          questionCount)
{
    if (!topic.empty())     m_state.topic      = topic;
    m_state.focusAreas  = focusAreas;
    if (!difficulty.empty()) m_state.difficulty = difficulty;
    if (questionCount > 0)  m_state.questions  = questionCount;
    Render();
}

// ---------------------------------------------------------------------------
// SaveFormState
// ---------------------------------------------------------------------------

void NewSessionPanel::SaveFormState() const {
    if (!m_activeProjectDir.empty()) {
        // Best-effort: try to get current form values from the WebView
        std::string json = ExtractFormJSON();

        ProjectConfig pcfg = LoadConfig(m_activeProjectDir);
        if (!json.empty()) {
            std::string topic = nsJsonField(json, "topic");
            std::string instr = nsJsonField(json, "instructions");
            std::string diff  = nsJsonField(json, "difficulty");
            std::string qs    = nsJsonField(json, "questions");
            std::string bk    = nsJsonField(json, "backend");
            std::string ak    = nsJsonField(json, "apiKey");
            std::string om    = nsJsonField(json, "ollamaModel");
            std::string fa    = nsJsonField(json, "focusAreas");
            std::string tc    = nsJsonField(json, "tidbitCount");

            if (!topic.empty()) pcfg.examTopic        = topic;
            if (!instr.empty()) pcfg.examInstructions = instr;
            if (!diff.empty())  pcfg.examFocusAreas   = fa;
            if (!bk.empty())    pcfg.examBackend       = bk;
            if (!ak.empty())    pcfg.examApiKey        = ak;
            if (!om.empty())    pcfg.examOllamaModel   = om;
            if (!tc.empty())    {
                try { pcfg.examTidbitCount = std::stoi(tc); } catch (...) {}
            }
            (void)qs;
        } else {
            // Fallback to in-memory state
            pcfg.examTopic        = m_state.topic;
            pcfg.examInstructions = m_state.instructions;
            pcfg.examFocusAreas   = SerializeFocusAreas(m_state.focusAreas);
            pcfg.examBackend      = m_state.backend;
            pcfg.examApiKey       = m_state.apiKey;
            pcfg.examOllamaModel  = m_state.ollamaModel;
            pcfg.examTidbitCount  = m_state.tidbitCount;
        }
        SaveConfig(m_activeProjectDir, pcfg);
    }

    AppState st = LoadAppState();
    st.lastExamProjectDir = m_activeProjectDir;
    SaveAppState(st);
}

// ---------------------------------------------------------------------------
// ExtractFormJSON
// ---------------------------------------------------------------------------

std::string NewSessionPanel::ExtractFormJSON() const {
    wxString json;
    m_webView->RunScript(
        "typeof nsCollectForm === 'function' ? nsCollectForm() : '{}'", &json);
    return json.ToStdString();
}

// ---------------------------------------------------------------------------
// GenerateSessionFilename
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
// OnNsAction — dispatch incoming JS messages
// ---------------------------------------------------------------------------

void NewSessionPanel::OnNsAction(wxWebViewEvent& evt) {
    std::string payload = evt.GetString().ToStdString();
    std::string action  = nsJsonField(payload, "action");

    if (action == "start")           HandleStart(payload);
    else if (action == "refresh-ollama") HandleRefreshOllama();
    else if (action == "open-context")   HandleOpenContext();
    else if (action == "reset-weights")  HandleResetWeights();
}

// ---------------------------------------------------------------------------
// HandleStart — parse JSON payload, build configs, launch session
// ---------------------------------------------------------------------------

void NewSessionPanel::HandleStart(const std::string& payload) {
    std::string topic = nsJsonField(payload, "topic");
    if (topic.empty()) {
        m_webView->RunScript("nsSetStatus('Enter a topic first.')");
        return;
    }
    if (m_activeProjectDir.empty()) {
        m_webView->RunScript("nsSetStatus('Activate a project in the Projects tab first.')");
        return;
    }

    std::string instr    = nsJsonField(payload, "instructions");
    std::string diff     = nsJsonField(payload, "difficulty");
    std::string qsStr    = nsJsonField(payload, "questions");
    std::string backend  = nsJsonField(payload, "backend");
    std::string apiKey   = nsJsonField(payload, "apiKey");
    std::string olModel  = nsJsonField(payload, "ollamaModel");
    std::string faStr    = nsJsonField(payload, "focusAreas");
    std::string tcStr    = nsJsonField(payload, "tidbitCount");
    std::string ucStr    = nsJsonField(payload, "useCorpus");

    int totalQ = 10;
    try { totalQ = std::stoi(qsStr); } catch (...) {}
    int tidCnt = 1;
    try { tidCnt = std::stoi(tcStr); } catch (...) {}
    bool useCorpus = (ucStr == "true");

    ExamConfig cfg;
    cfg.topic          = topic;
    cfg.instructions   = instr;
    cfg.focusAreaList  = DeserializeFocusAreas(faStr);
    cfg.difficulty     = diff.empty() ? "mixed" : diff;
    cfg.totalQuestions = totalQ;
    cfg.useCorpus      = useCorpus;
    cfg.personalities  = m_state.selectedPersonalities;
    cfg.tidbitCount    = tidCnt;

    // Load context.md if present
    std::string ctxPath = m_activeProjectDir + "/context.md";
    std::ifstream ctxFile(ctxPath);
    if (ctxFile)
        cfg.projectContext.assign(std::istreambuf_iterator<char>(ctxFile), {});

    LLMConfig llmCfg;
    llmCfg.backend     = BackendFromLabel(backend);
    llmCfg.apiKey      = apiKey;
    llmCfg.ollamaModel = olModel;
    cfg.largeModel     = IsLargeModel(llmCfg.backend);

    if (!InitProject(m_activeProjectDir)) {
        m_webView->RunScript("nsSetStatus('Cannot initialise project folder.')");
        return;
    }
    std::string sessionFile = m_activeProjectDir + "/" + GenerateSessionFilename();
    {
        std::ofstream f(sessionFile);
        if (!f) {
            m_webView->RunScript("nsSetStatus('Cannot create session file.')");
            return;
        }
        f << "# " << cfg.topic << " \xe2\x80\x94 Session\n\n"
          << "**Topic:** " << cfg.topic << "\n";
        if (!cfg.instructions.empty())
            f << "**Instructions:** " << cfg.instructions << "\n";
        f << "**Difficulty:** " << cfg.difficulty << "\n"
          << "**Questions:** " << cfg.totalQuestions << "\n"
          << "**Backend:** " << backend << "\n\n";
    }

    EnsureExamMeta(m_activeProjectDir, backend);
    SessionRecord rec;
    rec.sessionFile    = fs::path(sessionFile).filename().string();
    rec.startedAt      = MetaNow();
    rec.topic          = cfg.topic;
    rec.difficulty     = cfg.difficulty;
    rec.totalQuestions = cfg.totalQuestions;
    RecordSession(m_activeProjectDir, rec);

    // Update in-memory state and save
    m_state.topic        = topic;
    m_state.instructions = instr;
    m_state.focusAreas   = cfg.focusAreaList;
    m_state.backend      = backend;
    m_state.apiKey       = apiKey;
    m_state.ollamaModel  = olModel;
    m_state.tidbitCount  = tidCnt;
    SaveFormState();

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
                      + "  backend=" + backend);

    wxString statusScript = "nsSetStatus('Starting: " + wxString::FromUTF8(cfg.topic) + "')";
    m_webView->RunScript(statusScript);

    if (m_onStart)
        m_onStart(m_activeProjectDir, sessionFile, cfg, llmCfg);
}

// ---------------------------------------------------------------------------
// HandleRefreshOllama
// ---------------------------------------------------------------------------

void NewSessionPanel::HandleRefreshOllama() {
    auto models = FetchOllamaModels();
    m_state.ollamaModels = models;

    // Build a JS array literal and call nsSetOllamaModels
    std::string arr = "[";
    for (size_t i = 0; i < models.size(); ++i) {
        if (i) arr += ",";
        arr += "'" + models[i] + "'";
    }
    arr += "]";

    m_webView->RunScript("nsSetOllamaModels(" + arr + ")");

    std::string status = models.empty() ? "No Ollama models found." : "Loaded Ollama models.";
    m_webView->RunScript("nsSetStatus('" + status + "')");
}

// ---------------------------------------------------------------------------
// HandleOpenContext
// ---------------------------------------------------------------------------

void NewSessionPanel::HandleOpenContext() {
    if (m_activeProjectDir.empty()) {
        wxMessageBox("Activate a project first.", "No project", wxOK | wxICON_WARNING, this);
        return;
    }
    std::string ctxPath = m_activeProjectDir + "/context.md";
    if (!fs::exists(ctxPath)) {
        std::ofstream f(ctxPath);
        f << "# Study Context\n\n"
             "Paste your study notes, syllabus, or reference material here.\n"
             "This file is injected into every exam prompt and chat message.\n";
    }
    std::string cmd = "osascript -e 'tell application \"Terminal\" to do script "
                      "\"vim \\\"" + ctxPath + "\\\"\"'";
    wxExecute(wxString::FromUTF8(cmd), wxEXEC_ASYNC);
}

// ---------------------------------------------------------------------------
// HandleResetWeights
// ---------------------------------------------------------------------------

void NewSessionPanel::HandleResetWeights() {
    if (m_activeProjectDir.empty()) return;
    ProjectConfig pcfg = LoadConfig(m_activeProjectDir);
    pcfg.examMoreOf.clear();
    pcfg.examLessOf.clear();
    SaveConfig(m_activeProjectDir, pcfg);
    m_webView->RunScript("nsSetStatus('Topic weights cleared.')");
}

