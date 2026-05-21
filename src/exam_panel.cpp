#include "exam_panel.h"
#include "html_template.h"
#include "exam_prompt.h"
#include "exam_meta.h"
#include "project.h"
#include "corpus.h"
#include "markdown.h"
#include "logger.h"
#include "saved_convos.h"
#include "game_data.h"
#include "llm_response.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <wx/app.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

namespace fs = std::filesystem;

enum {
    ID_EXAM_SEND = wxID_HIGHEST + 100,
    ID_EXAM_SKIP,
    ID_EXAM_SILENT_SKIP,
    ID_EXAM_HINT,
    ID_EXAM_FLAG,
    ID_EXAM_ABANDON,
};

wxBEGIN_EVENT_TABLE(ExamPanel, wxPanel)
    EVT_BUTTON(ID_EXAM_SEND,        ExamPanel::OnSend)
    EVT_BUTTON(ID_EXAM_SKIP,        ExamPanel::OnSkip)
    EVT_BUTTON(ID_EXAM_SILENT_SKIP, ExamPanel::OnSilentSkip)
    EVT_BUTTON(ID_EXAM_HINT,        ExamPanel::OnHint)
    EVT_BUTTON(ID_EXAM_FLAG,        ExamPanel::OnFlag)
    EVT_BUTTON(ID_EXAM_ABANDON,     ExamPanel::OnAbandon)
    EVT_WEBVIEW_NAVIGATING(wxID_ANY, ExamPanel::OnWebViewNav)
wxEND_EVENT_TABLE()

ExamPanel::ExamPanel(wxWindow* parent,
                     SessionCompleteCallback onSessionComplete,
                     DeepDiveCallback        onDeepDive,
                     SavedConvoCallback      onSavedConvo)
    : wxPanel(parent),
      m_onComplete(std::move(onSessionComplete)),
      m_onDeepDive(std::move(onDeepDive)),
      m_onSavedConvo(std::move(onSavedConvo))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    // ── Splitter: left = exam view, right = side chat ─────────────────────
    m_splitter = new wxSplitterWindow(this, wxID_ANY,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxSP_3D | wxSP_LIVE_UPDATE);
    m_splitter->SetMinimumPaneSize(200);

    m_leftPanel = new wxPanel(m_splitter);
    auto* leftSizer = new wxBoxSizer(wxVERTICAL);

    m_webView = wxWebView::New(m_leftPanel, wxID_ANY, "about:blank");
    m_webView->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& evt) {
        if (m_splitter->IsSplit()) {
            m_splitter->Unsplit(m_chatPanel);
            m_chatOpen = false;
            m_webView->RunScript(
                "var o=document.getElementById('chat-overlay');"
                "if(o)o.classList.remove('active');");
        }
        evt.Skip();
    });
    leftSizer->Add(m_webView, 1, wxEXPAND);

    // ── Answer input row ──────────────────────────────────────────────────
    auto* inputRow = new wxBoxSizer(wxHORIZONTAL);
    m_answerCtrl = new wxTextCtrl(m_leftPanel, wxID_ANY, "",
        wxDefaultPosition, wxSize(-1, 80), wxTE_MULTILINE | wxTE_PROCESS_ENTER);
    m_answerCtrl->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& evt) {
        if (m_splitter->IsSplit()) {
            m_splitter->Unsplit(m_chatPanel);
            m_chatOpen = false;
            m_webView->RunScript(
                "var o=document.getElementById('chat-overlay');"
                "if(o)o.classList.remove('active');");
        }
        evt.Skip();
    });
    m_sendBtn       = new wxButton(m_leftPanel, ID_EXAM_SEND,        "Submit");
    m_skipBtn       = new wxButton(m_leftPanel, ID_EXAM_SKIP,        "I don't know");
    m_silentSkipBtn = new wxButton(m_leftPanel, ID_EXAM_SILENT_SKIP, wxString::FromUTF8("Skip \xe2\x8f\xad"));
    m_silentSkipBtn->SetToolTip("Skip this question silently — not sent to the LLM");
    m_hintBtn    = new wxButton(m_leftPanel, ID_EXAM_HINT,    wxString::FromUTF8("\xf0\x9f\x92\xa1 Hint"));
    m_hintBtn->SetToolTip("Get a nudge without revealing the answer");
    m_flagBtn    = new wxButton(m_leftPanel, ID_EXAM_FLAG,    "Flag for review");
    m_abandonBtn = new wxButton(m_leftPanel, ID_EXAM_ABANDON, "End Session");

    inputRow->Add(m_answerCtrl, 1, wxEXPAND | wxRIGHT, 4);
    auto* btnCol = new wxBoxSizer(wxVERTICAL);
    btnCol->Add(m_sendBtn,       0, wxEXPAND | wxBOTTOM, 4);
    btnCol->Add(m_skipBtn,       0, wxEXPAND | wxBOTTOM, 4);
    btnCol->Add(m_silentSkipBtn, 0, wxEXPAND | wxBOTTOM, 4);
    btnCol->Add(m_hintBtn,       0, wxEXPAND | wxBOTTOM, 4);
    btnCol->Add(m_flagBtn,       0, wxEXPAND | wxBOTTOM, 4);
    btnCol->Add(m_abandonBtn,    0, wxEXPAND);
    inputRow->Add(btnCol, 0, wxEXPAND);

    // Hint strip — hidden until a hint arrives
    m_hintCtrl = new wxTextCtrl(m_leftPanel, wxID_ANY, "",
        wxDefaultPosition, wxSize(-1, 56),
        wxTE_MULTILINE | wxTE_READONLY | wxTE_NO_VSCROLL | wxBORDER_NONE);
    m_hintCtrl->SetBackgroundColour(wxColour(255, 251, 230));
    m_hintCtrl->SetForegroundColour(wxColour(100, 70, 0));
    m_hintCtrl->Hide();

    m_statusLabel = new wxStaticText(m_leftPanel, wxID_ANY, "");

    leftSizer->Add(m_hintCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
    leftSizer->Add(inputRow, 0, wxEXPAND | wxALL, 6);
    leftSizer->Add(m_statusLabel, 0, wxLEFT | wxBOTTOM, 8);
    m_leftPanel->SetSizer(leftSizer);

    m_chatPanel = new TurnChatPanel(m_splitter, m_darkMode,
        [this]() {
            m_chatOpen = false;
            if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);
            m_webView->RunScript(
                "var o=document.getElementById('chat-overlay');"
                "if(o)o.classList.remove('active');");
        },
        [this]() { if (m_onSavedConvo) m_onSavedConvo(); });

    // Start unsplit — chat opens when the user clicks Discuss
    m_splitter->Initialize(m_leftPanel);

    outer->Add(m_splitter, 1, wxEXPAND);
    SetSizer(outer);

    // Start idle — no active session yet.
    m_sendBtn->Disable();
    m_skipBtn->Disable(); m_silentSkipBtn->Disable();
    m_hintBtn->Disable();
    m_flagBtn->Disable();
    m_abandonBtn->Disable();
    Render();
}

// ---------------------------------------------------------------------------
void ExamPanel::SetDarkMode(bool dark) {
    m_darkMode = dark;
    m_chatPanel->SetDarkMode(dark);
    if (dark) {
        m_answerCtrl->SetBackgroundColour(wxColour(28, 33, 40));
        m_answerCtrl->SetForegroundColour(wxColour(230, 237, 243));
    } else {
        m_answerCtrl->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        m_answerCtrl->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    }
    m_answerCtrl->Refresh();
    if (m_active) Render();
}

// ---------------------------------------------------------------------------
void ExamPanel::StartSession(const std::string& projectDir,
                             const std::string& sessionFile,
                             const ExamConfig&  cfg,
                             const LLMConfig&   llmCfg,
                             bool               darkMode) {
    // Keep previous session turns in history before starting fresh
    if (!m_turns.empty()) {
        std::string label = "Previous session";
        if (!m_sessionFile.empty()) {
            auto hdr = LoadSessionHeader(m_sessionFile);
            if (!hdr.topic.empty()) label = hdr.topic;
        }
        m_historyGroups.push_back({label, m_sessionFile, m_turns});
    }

    m_projectDir      = projectDir;
    m_sessionFile     = sessionFile;
    m_cfg             = cfg;
    m_llmCfg          = llmCfg;
    SetDarkMode(darkMode);
    {
        ProjectConfig pcfg = LoadConfig(projectDir);
        auto splitPipe = [](const std::string& s) {
            std::vector<std::string> v;
            std::istringstream ss(s);
            std::string tok;
            while (std::getline(ss, tok, '|'))
                if (!tok.empty()) v.push_back(tok);
            return v;
        };
        m_cfg.moreOfTopics = splitPipe(pcfg.examMoreOf);
        m_cfg.lessOfTopics = splitPipe(pcfg.examLessOf);
        if (pcfg.examTidbitCount >= 1 && pcfg.examTidbitCount <= 10)
            m_cfg.tidbitCount = pcfg.examTidbitCount;
    }
    m_active          = true;
    m_busy            = false;
    m_questionIndex   = 0;
    m_turns.clear();
    m_currentQuestion.clear();

    m_sendBtn->Enable();
    m_skipBtn->Enable(); m_silentSkipBtn->Enable();
    m_flagBtn->Disable(); // enabled after first question is answered
    m_abandonBtn->Enable();

    m_chatOpen = false;
    m_chatPanel->Reset();
    if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);

    Render(true);  // show history + loading state at the bottom immediately
    RequestFirstQuestion();
}

// ---------------------------------------------------------------------------
void ExamPanel::ResumeSession(const std::string& projectDir,
                               const std::string& sessionFile,
                               const LLMConfig&   llmCfg,
                               bool               darkMode) {
    auto turns = LoadSession(sessionFile);
    auto hdr   = LoadSessionHeader(sessionFile);
    if (hdr.topic.empty() && turns.empty()) return;

    m_projectDir      = projectDir;
    m_sessionFile     = sessionFile;
    m_llmCfg          = llmCfg;
    SetDarkMode(darkMode);
    m_turns           = turns;
    m_questionIndex   = (int)turns.size();
    m_currentQuestion.clear();

    // Load all older completed sessions into history
    m_historyGroups.clear();
    auto meta = LoadExamMeta(projectDir);
    for (const auto& rec : meta.sessions) {
        // Resolve path the same way review_panel does
        std::string path = projectDir + "/" + rec.sessionFile;
        if (!fs::exists(path)) {
            if (!fs::exists(rec.sessionFile)) continue;
            path = rec.sessionFile;
        }
        if (path == sessionFile) continue;  // skip the session we're resuming
        auto oldTurns = LoadSession(path);
        if (oldTurns.empty()) continue;
        std::string label = rec.topic.empty() ? rec.startedAt.substr(0, 10) : rec.topic;
        m_historyGroups.push_back({label, path, oldTurns});
    }

    m_cfg.topic          = hdr.topic;
    m_cfg.instructions   = hdr.instructions;
    m_cfg.difficulty     = hdr.difficulty;
    m_cfg.totalQuestions = hdr.totalQuestions;
    m_cfg.projectContext.clear();

    bool complete = (int)turns.size() >= hdr.totalQuestions;
    m_active = !complete;

    if (complete) {
        m_sendBtn->Disable();
        m_skipBtn->Disable(); m_silentSkipBtn->Disable();
        m_hintBtn->Disable();
        m_flagBtn->Enable(!turns.empty());
        m_abandonBtn->Disable();
        m_statusLabel->SetLabel("Session complete. See Review tab for results.");
    } else {
        // Session was interrupted mid-way — re-enable input, ask next question.
        m_sendBtn->Enable();
        m_skipBtn->Enable(); m_silentSkipBtn->Enable();
        m_hintBtn->Enable();
        m_flagBtn->Enable(!turns.empty());
        m_abandonBtn->Enable();
        int next = (int)turns.size() + 1;
        m_statusLabel->SetLabel("Resuming — question " + std::to_string(next)
                                + " of " + std::to_string(hdr.totalQuestions));
        RequestNextQuestion();
    }

    Render();
    Logger::get().log("Resumed session: " + sessionFile
                      + "  turns=" + std::to_string(turns.size())
                      + "  complete=" + std::to_string(complete));
}

// ---------------------------------------------------------------------------
void ExamPanel::StartDrill(const std::string& projectDir,
                           const std::string& sessionFile,
                           int                questionIndex,
                           const ExamConfig&  cfg,
                           const LLMConfig&   llmCfg,
                           bool               darkMode) {
    auto turns = LoadSession(sessionFile);
    if (questionIndex < 0 || questionIndex >= (int)turns.size()) return;

    m_projectDir    = projectDir;
    m_sessionFile   = sessionFile;
    m_cfg           = cfg;
    m_llmCfg        = llmCfg;
    SetDarkMode(darkMode);
    m_active        = true;
    m_busy          = false;
    m_turns         = {};
    m_questionIndex = 0;

    // Pre-load the flagged question as the current question.
    m_currentQuestion = turns[questionIndex].question;
    m_cfg.totalQuestions = 1;

    m_sendBtn->Enable();
    m_skipBtn->Enable(); m_silentSkipBtn->Enable();
    m_hintBtn->Enable();
    m_flagBtn->Enable();
    m_abandonBtn->Enable();

    m_statusLabel->SetLabel("Drill: " + m_currentQuestion.substr(0, 60));
    Render();
}

// ---------------------------------------------------------------------------
void ExamPanel::RequestFirstQuestion() {
    if (m_busy) return;
    m_busy = true;
    m_statusLabel->SetLabel("Asking first question…");
    m_sendBtn->Disable();
    m_skipBtn->Disable(); m_silentSkipBtn->Disable();
    m_hintBtn->Disable();

    ExamConfig  examCfg    = m_cfg;
    LLMConfig   llmCfg     = m_llmCfg;
    std::string projectDir = m_projectDir;

    std::thread([this, examCfg, llmCfg, projectDir]() {
        ExamConfig localCfg = examCfg;
        // Per-question random weighted pick from the focus-area list
        if (!localCfg.focusAreaList.empty())
            localCfg.focusAreas = PickFocusArea(localCfg.focusAreaList);
        if (localCfg.useCorpus) {
            std::string ragQuery = localCfg.focusAreas.empty()
                                   ? localCfg.topic : localCfg.focusAreas;
            std::string ctx = CorpusContextFor(projectDir, ragQuery,
                                               llmCfg.ollamaUrl, "Exam",
                                               CorpusTopK(llmCfg.backend));
            if (!ctx.empty()) localCfg.projectContext = ctx;
        }
        std::string prompt = BuildFirstQuestionPrompt(localCfg);
        auto result = InvokeLLM(prompt, llmCfg);
        wxTheApp->CallAfter([this, result]() {
            m_busy = false;
            if (!result.ok) {
                m_statusLabel->SetLabel("LLM error: " + result.error);
                Logger::get().log("ExamPanel LLM error: " + result.error);
                return;
            }
            m_currentQuestion = result.text;
            // Trim trailing whitespace/newlines
            while (!m_currentQuestion.empty() &&
                   (m_currentQuestion.back() == '\n' || m_currentQuestion.back() == '\r' ||
                    m_currentQuestion.back() == ' '))
                m_currentQuestion.pop_back();

            m_statusLabel->SetLabel("Question 1 of " + std::to_string(m_cfg.totalQuestions));
            m_sendBtn->Enable();
            m_skipBtn->Enable(); m_silentSkipBtn->Enable();
            m_hintBtn->Enable();
            m_hintBtn->SetLabel(wxString::FromUTF8("\xf0\x9f\x92\xa1 Hint"));
            m_hintCtrl->Clear(); m_hintCtrl->Hide();
            m_leftPanel->GetSizer()->Layout();
            Render(true);  // scroll to bottom to show the new question
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void ExamPanel::RequestNextQuestion() {
    if (m_busy) return;
    m_busy = true;
    m_sendBtn->Disable();
    m_skipBtn->Disable(); m_silentSkipBtn->Disable();
    m_hintBtn->Disable();
    m_statusLabel->SetLabel("Asking next question…");

    ExamConfig  examCfg    = m_cfg;
    LLMConfig   llmCfg     = m_llmCfg;
    std::string projectDir = m_projectDir;
    std::vector<QuestionTurn> turns = m_turns;
    int remaining = m_cfg.totalQuestions - (int)m_turns.size() - 1;
    std::string resumeQuery = m_turns.empty() ? m_cfg.topic : m_turns.back().question;

    std::thread([this, examCfg, llmCfg, projectDir, turns, remaining, resumeQuery]() {
        ExamConfig localCfg = examCfg;
        if (!localCfg.focusAreaList.empty())
            localCfg.focusAreas = PickFocusArea(localCfg.focusAreaList);
        if (localCfg.useCorpus) {
            std::string ragQuery = localCfg.focusAreas.empty()
                                   ? resumeQuery : localCfg.focusAreas;
            std::string ctx = CorpusContextFor(projectDir, ragQuery,
                                               llmCfg.ollamaUrl, "Exam",
                                               CorpusTopK(llmCfg.backend));
            if (!ctx.empty()) localCfg.projectContext = ctx;
        }
        std::string prompt = BuildScoringAndNextPrompt(
            localCfg, turns, "(session resumed — generate next question only)",
            "(resumed)", remaining);
        auto result = InvokeLLM(prompt, llmCfg);
        wxTheApp->CallAfter([this, result]() {
            m_busy = false;
            if (!result.ok) {
                m_statusLabel->SetLabel("LLM error: " + result.error);
                return;
            }
            auto scored = ParseScoredResponse(result.text);
            if (!scored.nextQuestion.empty()) {
                m_currentQuestion = scored.nextQuestion;
            } else {
                // Fallback: treat whole response as the question
                m_currentQuestion = result.text;
                while (!m_currentQuestion.empty() &&
                       (m_currentQuestion.back() == '\n' ||
                        m_currentQuestion.back() == ' '))
                    m_currentQuestion.pop_back();
            }
            int qNum = (int)m_turns.size() + 1;
            m_statusLabel->SetLabel("Question " + std::to_string(qNum)
                                    + " of " + std::to_string(m_cfg.totalQuestions));
            m_sendBtn->Enable();
            m_skipBtn->Enable(); m_silentSkipBtn->Enable();
            m_hintBtn->Enable();
            m_hintBtn->SetLabel(wxString::FromUTF8("\xf0\x9f\x92\xa1 Hint"));
            m_hintCtrl->Clear(); m_hintCtrl->Hide();
            m_leftPanel->GetSizer()->Layout();
            Render(true);  // scroll to bottom to show the new question
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void ExamPanel::SubmitAnswer(const std::string& answer) {
    if (m_busy || m_currentQuestion.empty()) return;
    m_busy = true;
    m_sendBtn->Disable();
    m_skipBtn->Disable(); m_silentSkipBtn->Disable();
    m_hintBtn->Disable();
    m_statusLabel->SetLabel("Scoring…");

    int         remaining       = m_cfg.totalQuestions - (int)m_turns.size() - 1;
    ExamConfig  examCfg         = m_cfg;
    LLMConfig   llmCfg          = m_llmCfg;
    std::string projectDir      = m_projectDir;
    std::string currentQuestion = m_currentQuestion;
    std::vector<QuestionTurn> turns = m_turns;

    std::thread([this, answer, examCfg, llmCfg, projectDir, currentQuestion, turns, remaining]() {
        ExamConfig localCfg = examCfg;
        if (!localCfg.focusAreaList.empty())
            localCfg.focusAreas = PickFocusArea(localCfg.focusAreaList);
        if (localCfg.useCorpus) {
            std::string ragQuery = localCfg.focusAreas.empty()
                                   ? currentQuestion : localCfg.focusAreas;
            std::string ctx = CorpusContextFor(projectDir, ragQuery,
                                               llmCfg.ollamaUrl, "Exam",
                                               CorpusTopK(llmCfg.backend));
            if (!ctx.empty()) localCfg.projectContext = ctx;
        }
        std::string prompt = BuildScoringAndNextPrompt(
            localCfg, turns, currentQuestion, answer, remaining);
        auto result = InvokeLLM(prompt, llmCfg);
        wxTheApp->CallAfter([this, answer, result, remaining]() {
            m_busy = false;
            if (!result.ok) {
                m_statusLabel->SetLabel("LLM error: " + result.error);
                Logger::get().log("ExamPanel score error: " + result.error);
                m_sendBtn->Enable();
                m_skipBtn->Enable(); m_silentSkipBtn->Enable();
                m_hintBtn->Enable();
                return;
            }

            auto scored = ParseScoredResponse(result.text);
            if (!scored.parseOk) {
                scored.score       = Score::Star1;
                scored.explanation = result.text;
                scored.parseOk     = true;
            }
            // App owns the skipped state — the model never picks it
            if (answer.empty()) scored.score = Score::Skipped;
            // If model omitted EXPLANATION, use the full raw response
            if (scored.explanation.empty()) scored.explanation = result.text;

            QuestionTurn turn;
            turn.question    = m_currentQuestion;
            turn.userAnswer  = answer;
            turn.score       = scored.score;
            turn.explanation = scored.explanation;
            turn.flagged     = false;
            m_turns.push_back(turn);
            AppendSessionTurn(m_sessionFile, turn);

            m_flagBtn->Enable();
            ++m_questionIndex;

            if (!scored.nextQuestion.empty()) {
                m_currentQuestion = scored.nextQuestion;
                int qNum = (int)m_turns.size() + 1;
                m_statusLabel->SetLabel("Question " + std::to_string(qNum)
                    + " of " + std::to_string(m_cfg.totalQuestions));
                m_sendBtn->Enable();
                m_skipBtn->Enable(); m_silentSkipBtn->Enable();
                m_hintBtn->Enable();
                m_hintBtn->SetLabel(wxString::FromUTF8("\xf0\x9f\x92\xa1 Hint"));
                m_hintCtrl->Clear(); m_hintCtrl->Hide();
                m_leftPanel->GetSizer()->Layout();
                m_answerCtrl->Clear();
            } else {
                // Session complete — keep lastSessionFile so Exam tab can show
                // the completed turns read-only on next startup.
                m_active          = false;
                m_currentQuestion.clear();
                m_statusLabel->SetLabel("Session complete! See Review tab for results.");
                m_sendBtn->Disable();
                m_skipBtn->Disable(); m_silentSkipBtn->Disable();
                m_hintBtn->Disable();
                m_hintCtrl->Clear(); m_hintCtrl->Hide();
                m_leftPanel->GetSizer()->Layout();
                m_abandonBtn->Disable();
                if (m_onComplete) m_onComplete(m_sessionFile);
            }
            Render(true);  // scroll to bottom to show the scored answer + next question
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void ExamPanel::Render(bool scrollToBottom) {
    std::string html = BuildExamHTML(scrollToBottom);
    m_webView->SetPage(wxString::FromUTF8(html), "");
}

// ---------------------------------------------------------------------------
std::string ExamPanel::BuildExamHTML(bool scrollToBottom) const {
    std::ostringstream body;

    if (!m_historyGroups.empty())
        body << RenderHistoryGroups(m_historyGroups);

    if (!m_active && m_turns.empty() && m_historyGroups.empty()) {
        body << "<p style='color:var(--text-muted);margin-top:2em'>"
                "No active session. Go to <strong>New Session</strong> to start.</p>";
    } else if (!m_turns.empty() || m_active) {
        // Load chat counts for each completed turn so the discuss button can
        // show a badge when a turn already has follow-up exchanges.
        std::vector<int> chatCounts;
        chatCounts.reserve(m_turns.size());
        for (int i = 0; i < (int)m_turns.size(); ++i)
            chatCounts.push_back((int)LoadTurnChat(m_sessionFile, i).size());

        body << RenderExamTurns(m_turns, chatCounts,
                                m_cfg.moreOfTopics, m_cfg.lessOfTopics);

        if (!m_currentQuestion.empty()) {
            body << "<div class='current-question'>"
                 << RenderMarkdown(m_currentQuestion)
                 << "</div>";
        }
        if (!m_active && !m_turns.empty()) {
            body << "<p class='done'>Session complete. Check the Review tab.</p>";
        }
    }

    std::string extraCSS = R"(<style>
.current-question { background:var(--surface); border:2px solid var(--link);
                    border-radius:6px; padding:1em; margin-top:1.5em;
                    font-size:1.05em; }
.done { color:var(--text-muted); margin-top:1.5em; }
#chat-overlay { display:none; position:fixed; inset:0; z-index:9999;
                cursor:pointer; background:transparent; }
#chat-overlay.active { display:block; }
#deepdive-btn { position:fixed; bottom:1.4em; right:1.4em; z-index:100;
                background:var(--link); color:#fff; border:none;
                border-radius:50%; width:48px; height:48px;
                font-size:1.4em; cursor:pointer; box-shadow:0 2px 8px rgba(0,0,0,.35);
                display:flex; align-items:center; justify-content:center;
                text-decoration:none; }
#deepdive-btn:hover { opacity:.85; }
</style>
)";

    // Overlay is always in the DOM so RunScript can toggle it without a full reload.
    std::string activeClass = m_chatOpen ? " class='active'" : "";
    std::string overlay = "<div id='chat-overlay'" + activeClass
                        + " onclick=\"window.location='testtaker://closechat'\"></div>";

    std::string deepdiveBtn;
    if (m_active)
        deepdiveBtn = "<a id='deepdive-btn' href='testtaker://deepdive' "
                      "title='Set focus areas for remaining questions'>&#x1F3AF;</a>";

    body << "<div id='page-bottom'></div>";
    if (scrollToBottom)
        body << "<script>requestAnimationFrame(function(){"
                "requestAnimationFrame(function(){"
                "var b=document.getElementById('page-bottom');"
                "if(b)b.scrollIntoView({behavior:'instant'});"
                "});});</script>";

    return BuildHTML(extraCSS + overlay + deepdiveBtn + body.str(), "Exam", m_darkMode);
}

// ---------------------------------------------------------------------------
void ExamPanel::OnSend(wxCommandEvent&) {
    std::string answer = m_answerCtrl->GetValue().ToStdString();
    if (answer.empty()) return;
    SubmitAnswer(answer);
}

void ExamPanel::OnSkip(wxCommandEvent&) {
    SubmitAnswer("");
}

void ExamPanel::OnSilentSkip(wxCommandEvent&) {
    if (m_busy || m_currentQuestion.empty()) return;

    QuestionTurn turn;
    turn.question   = m_currentQuestion;
    turn.score      = Score::Skipped;
    turn.silentSkip = true;
    m_turns.push_back(turn);
    AppendSessionTurn(m_sessionFile, turn);

    m_currentQuestion.clear();
    m_flagBtn->Enable();
    ++m_questionIndex;

    if (m_questionIndex < m_cfg.totalQuestions) {
        RequestNextQuestion();
    } else {
        m_active = false;
        m_sendBtn->Disable();
        m_skipBtn->Disable(); m_silentSkipBtn->Disable();
        m_abandonBtn->Disable();
        m_statusLabel->SetLabel("Session complete! See Review tab for results.");
        if (m_onComplete) m_onComplete(m_sessionFile);
        Render(true);
    }
}

// ---------------------------------------------------------------------------
void ExamPanel::OnHint(wxCommandEvent&) {
    if (m_busy || m_currentQuestion.empty()) return;

    m_hintBtn->SetLabel("⏳ …");
    m_hintBtn->Disable();

    std::string question = m_currentQuestion;
    LLMConfig   llmCfg   = m_llmCfg;

    std::thread([=]() {
        LLMResult result = InvokeLLM(BuildHintPrompt(question), llmCfg);
        wxTheApp->CallAfter([=]() {
            std::string hint = result.ok ? result.text : "(could not fetch hint)";
            // Strip leading/trailing whitespace
            while (!hint.empty() && (hint.front() == '\n' || hint.front() == ' '))
                hint.erase(hint.begin());
            while (!hint.empty() && (hint.back() == '\n' || hint.back() == ' '))
                hint.pop_back();

            m_hintCtrl->SetValue(wxString::FromUTF8("💡 " + hint));
            m_hintCtrl->Show();
            m_leftPanel->GetSizer()->Layout();

            m_hintBtn->SetLabel(wxString::FromUTF8("\xf0\x9f\x92\xa1 Hint"));
            m_hintBtn->Enable();
        });
    }).detach();
}

static std::string QuestionSnippet(const std::string& q) {
    std::string s = q.substr(0, 80);
    auto nl = s.find('\n');
    if (nl != std::string::npos) s = s.substr(0, nl);
    if (s.size() > 60) s = s.substr(0, 57) + "...";
    return s;
}

static void AddToTopicList(std::vector<std::string>& list, const std::string& item) {
    for (const auto& e : list)
        if (e == item) return;
    list.push_back(item);
}

static std::string JoinPipeVec(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += '|';
        s += v[i];
    }
    return s;
}

void ExamPanel::OnAbandon(wxCommandEvent&) {
    int ans = wxMessageBox(
        "End this session now?\n\nProgress so far will be saved to the Review tab.",
        "End Session", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
    if (ans != wxYES) return;
    AbandonSession();
}

void ExamPanel::Clear() {
    m_active          = false;
    m_busy            = false;
    m_turns.clear();
    m_historyGroups.clear();
    m_currentQuestion.clear();
    m_sessionFile.clear();
    m_projectDir.clear();

    m_sendBtn->Disable();
    m_skipBtn->Disable(); m_silentSkipBtn->Disable();
    m_flagBtn->Disable();
    m_abandonBtn->Disable();
    m_statusLabel->SetLabel("");

    m_chatOpen = false;
    m_chatPanel->Reset();
    if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);

    Render();
}

void ExamPanel::AbandonSession() {
    m_active          = false;
    m_busy            = false;
    m_currentQuestion.clear();

    m_sendBtn->Disable();
    m_skipBtn->Disable(); m_silentSkipBtn->Disable();
    m_abandonBtn->Disable();
    m_statusLabel->SetLabel("Session ended. See Review tab for results.");

    if (!m_projectDir.empty()) {
        ProjectConfig pcfg = LoadConfig(m_projectDir);
        pcfg.lastSession.clear();
        SaveConfig(m_projectDir, pcfg);
    }

    if (m_onComplete) m_onComplete(m_sessionFile);
    Render();
    Logger::get().log("Session abandoned: " + m_sessionFile);
}

void ExamPanel::OnFlag(wxCommandEvent&) {
    if (m_turns.empty()) return;
    int idx = (int)m_turns.size() - 1;
    m_turns[idx].flagged = !m_turns[idx].flagged;
    SetTurnFlagged(m_sessionFile, idx, m_turns[idx].flagged);
    m_flagBtn->SetLabel(m_turns[idx].flagged ? "Unflag" : "Flag for review");
    Render();
}

void ExamPanel::OnWebViewNav(wxWebViewEvent& evt) {
    wxString url = evt.GetURL();

    if (url.StartsWith("testtaker://flag/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(17).ToLong(&idx);  // "testtaker://flag/" is 17 chars
        if (idx < 0 || idx >= (long)m_turns.size()) return;

        m_turns[idx].flagged = !m_turns[idx].flagged;
        SetTurnFlagged(m_sessionFile, idx, m_turns[idx].flagged);
        Logger::get().log("Turn " + std::to_string(idx)
                          + " flagged=" + std::to_string(m_turns[idx].flagged));

        if (idx == (long)m_turns.size() - 1)
            m_flagBtn->SetLabel(m_turns[idx].flagged ? "Unflag" : "Flag for review");

        Render();
        return;
    }

    if (url.StartsWith("testtaker://discuss/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(20).ToLong(&idx);  // "testtaker://discuss/" is 20 chars
        if (idx < 0 || idx >= (long)m_turns.size()) return;

        m_chatPanel->OpenTurn(m_turns[idx], (int)idx, m_sessionFile, m_llmCfg);

        if (!m_splitter->IsSplit()) {
            int w = m_splitter->GetClientSize().GetWidth();
            m_splitter->SplitVertically(m_leftPanel, m_chatPanel, w * 6 / 10);
        }
        m_chatOpen = true;
        // Toggle overlay via script — avoids a full SetPage() that would reset scroll.
        m_webView->RunScript(
            "var o=document.getElementById('chat-overlay');"
            "if(o)o.classList.add('active');");
        return;
    }

    if (url == "testtaker://closechat") {
        evt.Veto();
        m_chatOpen = false;
        if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);
        m_webView->RunScript(
            "var o=document.getElementById('chat-overlay');"
            "if(o)o.classList.remove('active');");
        return;
    }

    if (url == "testtaker://deepdive") {
        evt.Veto();
        if (m_onDeepDive) m_onDeepDive();
        return;
    }

    if (url.StartsWith("testtaker://note/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(17).ToLong(&idx);  // "testtaker://note/" is 17 chars
        if (idx < 0 || idx >= (long)m_turns.size()) return;

        wxString current = wxString::FromUTF8(m_turns[idx].note);
        wxTextEntryDialog dlg(this, "Your note for this question:", "Add Note", current,
                              wxOK | wxCANCEL | wxTE_MULTILINE);
        if (dlg.ShowModal() != wxID_OK) return;

        std::string note = dlg.GetValue().ToStdString();
        m_turns[idx].note = note;
        SetTurnNote(m_sessionFile, idx, note);
        Logger::get().log("Turn " + std::to_string(idx) + " note set");
        Render();
        return;
    }

    // ── History-turn actions: testtaker://h{action}/groupIdx/turnIdx ──────
    auto parseHistIdx = [](const wxString& rest, long& g, long& i) -> bool {
        int slash = rest.Find('/');
        if (slash < 0) return false;
        return rest.Left(slash).ToLong(&g) && rest.Mid(slash + 1).ToLong(&i);
    };

    if (url.StartsWith("testtaker://hflag/")) {
        evt.Veto();
        long g = -1, i = -1;
        if (!parseHistIdx(url.Mid(18), g, i)) return;
        if (g < 0 || g >= (long)m_historyGroups.size()) return;
        if (i < 0 || i >= (long)m_historyGroups[g].turns.size()) return;
        auto& turn = m_historyGroups[g].turns[i];
        turn.flagged = !turn.flagged;
        SetTurnFlagged(m_historyGroups[g].sessionFile, (int)i, turn.flagged);
        Render();
        return;
    }

    if (url.StartsWith("testtaker://hnote/")) {
        evt.Veto();
        long g = -1, i = -1;
        if (!parseHistIdx(url.Mid(18), g, i)) return;
        if (g < 0 || g >= (long)m_historyGroups.size()) return;
        if (i < 0 || i >= (long)m_historyGroups[g].turns.size()) return;
        auto& turn = m_historyGroups[g].turns[i];
        wxTextEntryDialog dlg(this, "Your note for this question:", "Add Note",
                              wxString::FromUTF8(turn.note),
                              wxOK | wxCANCEL | wxTE_MULTILINE);
        if (dlg.ShowModal() != wxID_OK) return;
        turn.note = dlg.GetValue().ToStdString();
        SetTurnNote(m_historyGroups[g].sessionFile, (int)i, turn.note);
        Render();
        return;
    }

    if (url.StartsWith("testtaker://hdiscuss/")) {
        evt.Veto();
        long g = -1, i = -1;
        if (!parseHistIdx(url.Mid(21), g, i)) return;
        if (g < 0 || g >= (long)m_historyGroups.size()) return;
        if (i < 0 || i >= (long)m_historyGroups[g].turns.size()) return;
        const auto& grp = m_historyGroups[g];
        m_chatPanel->OpenTurn(grp.turns[i], (int)i, grp.sessionFile, m_llmCfg);
        if (!m_splitter->IsSplit()) {
            int w = m_splitter->GetClientSize().GetWidth();
            m_splitter->SplitVertically(m_leftPanel, m_chatPanel, w * 6 / 10);
        }
        m_chatOpen = true;
        m_webView->RunScript(
            "var o=document.getElementById('chat-overlay');"
            "if(o)o.classList.add('active');");
        return;
    }

    if (url.StartsWith("testtaker://hsave/")) {
        evt.Veto();
        long g = -1, i = -1;
        if (!parseHistIdx(url.Mid(18), g, i)) return;
        if (g < 0 || g >= (long)m_historyGroups.size()) return;
        if (i < 0 || i >= (long)m_historyGroups[g].turns.size()) return;
        auto& turn = m_historyGroups[g].turns[i];
        if (turn.saved) return;

        std::time_t now = std::time(nullptr);
        char dateBuf[16];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", std::localtime(&now));

        AppendSavedConvo(m_projectDir, turn.question, turn.explanation, dateBuf);
        turn.saved = true;
        SetTurnSaved(m_historyGroups[g].sessionFile, (int)i, true);
        if (m_onSavedConvo) m_onSavedConvo();
        Render();
        return;
    }

    if (url == "testtaker://clear-history") {
        evt.Veto();
        m_historyGroups.clear();
        Render();
        return;
    }

    if (url.StartsWith("testtaker://save/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(17).ToLong(&idx);  // "testtaker://save/" is 17 chars
        if (idx < 0 || idx >= (long)m_turns.size()) return;
        if (m_turns[idx].saved) return;  // already saved — no duplicates

        // ISO date string from current time
        std::time_t now = std::time(nullptr);
        char dateBuf[16];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", std::localtime(&now));

        const auto& t = m_turns[idx];
        AppendSavedConvo(m_projectDir, t.question, t.explanation, dateBuf);

        m_turns[idx].saved = true;
        SetTurnSaved(m_sessionFile, idx, true);
        Logger::get().log("Turn " + std::to_string(idx) + " saved to saved_convos.md");

        if (m_onSavedConvo) m_onSavedConvo();
        // Update the button in-place — avoids a full SetPage() that resets scroll.
        std::string script =
            "var b=document.querySelector(\"a[href='testtaker://save/"
            + std::to_string(idx) + "']\");"
            "if(b){b.classList.add('saved');b.innerHTML='🔖 saved';}";
        m_webView->RunScript(script);
        return;
    }

    if (url.StartsWith("testtaker://learnmore/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(22).ToLong(&idx);  // "testtaker://learnmore/" is 22 chars
        if (idx < 0 || idx >= (long)m_turns.size()) return;
        const auto& t = m_turns[idx];
        if (t.explanation.empty()) return;

        std::string idxStr = std::to_string(idx);
        m_webView->RunScript(
            "var b=document.querySelector(\"a[href='testtaker://learnmore/" + idxStr + "']\");"
            "if(b){b.classList.add('saving');b.innerHTML='&#x23F3; loading…';b.style.pointerEvents='none';}");

        std::string question    = t.question;
        std::string explanation = t.explanation;
        LLMConfig   llmCfg      = m_llmCfg;
        std::string projectDir  = m_projectDir;
        auto        onSaved     = m_onSavedConvo;

        std::thread([=]() {
            LLMResult result = InvokeLLM(BuildLearnMorePrompt(question, explanation), llmCfg);
            wxTheApp->CallAfter([=]() {
                // Restore button regardless of outcome
                m_webView->RunScript(
                    "var b=document.querySelector(\"a[href='testtaker://learnmore/" + idxStr + "']\");"
                    "if(b){b.classList.remove('saving');b.classList.add('done');"
                    "b.innerHTML='&#x1F4D6; saved';b.style.pointerEvents='none';}");

                if (!result.text.empty() && !projectDir.empty()) {
                    std::time_t now = std::time(nullptr);
                    char dateBuf[16];
                    std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", std::localtime(&now));
                    AppendSavedConvo(projectDir, question, result.text, dateBuf);
                    if (onSaved) onSaved();
                }
            });
        }).detach();
        return;
    }

    if (url.StartsWith("testtaker://hgame/")) {
        evt.Veto();
        long g = -1, i = -1;
        auto parseHistIdx = [](const wxString& rest, long& g, long& i) -> bool {
            int slash = rest.Find('/');
            if (slash < 0) return false;
            return rest.Left(slash).ToLong(&g) && rest.Mid(slash + 1).ToLong(&i);
        };
        if (!parseHistIdx(url.Mid(18), g, i)) return;
        if (g < 0 || g >= (long)m_historyGroups.size()) return;
        if (i < 0 || i >= (long)m_historyGroups[g].turns.size()) return;
        const QuestionTurn& turn = m_historyGroups[g].turns[i];
        if (turn.explanation.empty()) return;

        std::string gStr = std::to_string(g), iStr = std::to_string(i);
        std::string href = "testtaker://hgame/" + gStr + "/" + iStr;
        m_webView->RunScript(
            "var g=document.querySelector(\"a[href='" + href + "']\");"
            "if(g){g.textContent='⏳ generating…';g.style.pointerEvents='none';}");

        std::string question    = turn.question;
        std::string explanation = turn.explanation;
        LLMConfig   llmCfg      = m_llmCfg;

        std::thread([=]() {
            LLMResult r1, r2;
            {
                auto t1 = std::thread([&]() {
                    r1 = InvokeLLM(BuildGameSeriesPrompt(question, explanation, 4), llmCfg);
                });
                auto t2 = std::thread([&]() {
                    r2 = InvokeLLM(BuildGameSeriesPrompt(question, explanation, 4), llmCfg);
                });
                t1.join(); t2.join();
            }
            wxTheApp->CallAfter([=]() {
                m_webView->RunScript(
                    "var g=document.querySelector(\"a[href='" + href + "']\");"
                    "if(g){g.textContent='🎮 game';g.style.pointerEvents='';}");

                auto b1 = ParseMultipleGameChoices(r1.text);
                auto b2 = ParseMultipleGameChoices(r2.text);
                if (b1.empty() && b2.empty()) { Logger::get().log("hgame parse failed"); return; }

                auto toGameData = [&](const std::vector<GameQuestionBlock>& bl) {
                    std::vector<GameData> out;
                    for (auto& b : bl) {
                        bool cia = (std::rand() % 2 == 0);
                        GameData gd;
                        gd.question   = b.question.empty() ? question : b.question;
                        gd.choiceA    = cia ? b.correct : b.wrong;
                        gd.choiceB    = cia ? b.wrong   : b.correct;
                        gd.correctIsA = cia;
                        out.push_back(gd);
                    }
                    return out;
                };
                auto all = toGameData(b1);
                auto m2  = toGameData(b2);
                all.insert(all.end(), m2.begin(), m2.end());

                wxString tmpPath = wxFileName::CreateTempFileName("tt-game") + ".dat";
                if (!WriteGameFiles(tmpPath.ToStdString(), all)) {
                    Logger::get().log("hgame: could not write game data file"); return;
                }
                wxFileName exeDir(wxStandardPaths::Get().GetExecutablePath());
                wxString gameBin = exeDir.GetPath() + "/test-taker-game";
                if (!wxFileExists(gameBin))
                    gameBin = exeDir.GetPath() + "/../test-taker-game";
                if (!wxFileExists(gameBin)) {
                    Logger::get().log("test-taker-game binary not found"); return;
                }
                wxExecute("\"" + gameBin + "\" \"" + tmpPath + "\"", wxEXEC_ASYNC);
                Logger::get().log("hgame: launched with " + std::to_string(all.size()) + " questions");

                std::string tmpStr    = tmpPath.ToStdString();
                std::string wantFile  = tmpStr + ".want";
                std::string saveFile  = tmpStr + ".save";
                std::string hintReq   = tmpStr + ".hintreq";
                std::string hintResp  = tmpStr + ".hintresp";
                std::string pDir      = m_projectDir;
                auto        onSaved   = m_onSavedConvo;
                std::thread([=]() {
                    for (;;) {
                        if (fs::exists(saveFile)) {
                            std::string q, a;
                            { std::ifstream sf(saveFile);
                              std::string ln;
                              while (std::getline(sf, ln)) {
                                  if (ln.rfind("Q: ", 0) == 0) q = ln.substr(3);
                                  if (ln.rfind("A: ", 0) == 0) a = ln.substr(3);
                              } }
                            try { fs::remove(saveFile); } catch (...) {}
                            if (!q.empty() && !a.empty() && !pDir.empty()) {
                                char buf[32]; time_t now = time(nullptr);
                                std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M",
                                              std::localtime(&now));
                                AppendSavedConvo(pDir, q, "Correct answer: " + a, buf, true);
                                if (onSaved) wxTheApp->CallAfter(onSaved);
                            }
                        }
                        if (fs::exists(hintReq)) {
                            std::string hq, ha, hb;
                            { std::ifstream hf(hintReq);
                              std::string ln;
                              while (std::getline(hf, ln)) {
                                  if (ln.rfind("Q: ", 0) == 0) hq = ln.substr(3);
                                  if (ln.rfind("A: ", 0) == 0) ha = ln.substr(3);
                                  if (ln.rfind("B: ", 0) == 0) hb = ln.substr(3);
                              } }
                            try { fs::remove(hintReq); } catch (...) {}
                            if (!hq.empty()) {
                                LLMResult hr = InvokeLLM(
                                    BuildGameHintPrompt(hq, ha, hb), llmCfg);
                                if (hr.ok && !hr.text.empty()) {
                                    std::ofstream hrf(hintResp);
                                    hrf << hr.text;
                                }
                            }
                        }
                        if (fs::exists(wantFile)) {
                            try { fs::remove(wantFile); } catch (...) {}
                            LLMResult r = InvokeLLM(
                                BuildGameSeriesPrompt(question, explanation, 4), llmCfg);
                            if (r.ok) {
                                auto bl = ParseMultipleGameChoices(r.text);
                                std::vector<GameData> batch;
                                for (auto& b : bl) {
                                    bool cia = (std::rand() % 2 == 0);
                                    GameData gd;
                                    gd.question   = b.question.empty() ? question : b.question;
                                    gd.choiceA    = cia ? b.correct : b.wrong;
                                    gd.choiceB    = cia ? b.wrong   : b.correct;
                                    gd.correctIsA = cia;
                                    batch.push_back(gd);
                                }
                                if (!batch.empty()) AppendGameFiles(tmpStr, batch);
                            }
                        }
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                    }
                }).detach();
            });
        }).detach();
        return;
    }

    if (url.StartsWith("testtaker://game/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(17).ToLong(&idx);
        if (idx < 0 || idx >= (long)m_turns.size()) return;
        const QuestionTurn& turn = m_turns[idx];
        if (turn.explanation.empty()) return;

        // Disable the button while generating to prevent double-launches.
        std::string si = std::to_string(idx);
        m_webView->RunScript(
            "var g=document.querySelector(\"a[href='testtaker://game/" + si + "']\");"
            "if(g){g.textContent='⏳ generating…';g.style.pointerEvents='none';}");

        std::string question    = turn.question;
        std::string explanation = turn.explanation;
        std::string projectDir  = m_projectDir;
        LLMConfig   llmCfg     = m_llmCfg;
        auto cancel = std::make_shared<std::atomic<bool>>(false);

        std::thread([=]() {
            // Two batches of 4 in parallel → 8 questions ready before launch.
            LLMResult r1, r2;
            {
                auto t1 = std::thread([&]() {
                    r1 = InvokeLLM(BuildGameSeriesPrompt(question, explanation, 4), llmCfg);
                });
                auto t2 = std::thread([&]() {
                    r2 = InvokeLLM(BuildGameSeriesPrompt(question, explanation, 4), llmCfg);
                });
                t1.join(); t2.join();
            }

            wxTheApp->CallAfter([=]() {
                // Restore button
                m_webView->RunScript(
                    "var g=document.querySelector(\"a[href='testtaker://game/" + si + "']\");"
                    "if(g){g.textContent='🎮 game';g.style.pointerEvents='';}");

                auto b1 = ParseMultipleGameChoices(r1.text);
                auto b2 = ParseMultipleGameChoices(r2.text);
                if (b1.empty() && b2.empty()) {
                    Logger::get().log("Game series parse failed");
                    return;
                }

                auto toGameData = [&](const std::vector<GameQuestionBlock>& bl) {
                    std::vector<GameData> out;
                    for (auto& b : bl) {
                        bool cia = (std::rand() % 2 == 0);
                        GameData gd;
                        gd.question   = b.question.empty() ? question : b.question;
                        gd.choiceA    = cia ? b.correct : b.wrong;
                        gd.choiceB    = cia ? b.wrong   : b.correct;
                        gd.correctIsA = cia;
                        out.push_back(gd);
                    }
                    return out;
                };

                auto all = toGameData(b1);
                auto m2  = toGameData(b2);
                all.insert(all.end(), m2.begin(), m2.end());

                wxString tmpPath = wxFileName::CreateTempFileName("tt-game") + ".dat";
                if (!WriteGameFiles(tmpPath.ToStdString(), all)) {
                    Logger::get().log("Could not write game data file");
                    return;
                }

                wxFileName exeDir(wxStandardPaths::Get().GetExecutablePath());
                wxString gameBin = exeDir.GetPath() + "/test-taker-game";
                if (!wxFileExists(gameBin))
                    gameBin = exeDir.GetPath() + "/../test-taker-game";
                if (!wxFileExists(gameBin)) {
                    Logger::get().log("test-taker-game binary not found near " + gameBin.ToStdString());
                    return;
                }

                wxString cmd = "\"" + gameBin + "\" \"" + tmpPath + "\"";
                wxExecute(cmd, wxEXEC_ASYNC);
                Logger::get().log("Launched game with " + std::to_string(all.size()) + " questions");

                // Monitor .want, .save, and .hintreq signals from the game process.
                std::string tmpStr   = tmpPath.ToStdString();
                std::string wantFile = tmpStr + ".want";
                std::string saveFile = tmpStr + ".save";
                std::string hintReq  = tmpStr + ".hintreq";
                std::string hintResp = tmpStr + ".hintresp";
                auto        onSaved  = m_onSavedConvo;
                std::thread([=]() {
                    for (;;) {
                        if (fs::exists(saveFile)) {
                            std::string q, a;
                            { std::ifstream sf(saveFile);
                              std::string ln;
                              while (std::getline(sf, ln)) {
                                  if (ln.rfind("Q: ", 0) == 0) q = ln.substr(3);
                                  if (ln.rfind("A: ", 0) == 0) a = ln.substr(3);
                              } }
                            try { fs::remove(saveFile); } catch (...) {}
                            if (!q.empty() && !a.empty() && !projectDir.empty()) {
                                char buf[32]; time_t now = time(nullptr);
                                std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M",
                                              std::localtime(&now));
                                AppendSavedConvo(projectDir, q,
                                                 "Correct answer: " + a, buf, true);
                                if (onSaved) wxTheApp->CallAfter(onSaved);
                            }
                        }
                        if (fs::exists(hintReq)) {
                            std::string hq, ha, hb;
                            { std::ifstream hf(hintReq);
                              std::string ln;
                              while (std::getline(hf, ln)) {
                                  if (ln.rfind("Q: ", 0) == 0) hq = ln.substr(3);
                                  if (ln.rfind("A: ", 0) == 0) ha = ln.substr(3);
                                  if (ln.rfind("B: ", 0) == 0) hb = ln.substr(3);
                              } }
                            try { fs::remove(hintReq); } catch (...) {}
                            if (!hq.empty()) {
                                LLMResult hr = InvokeLLM(
                                    BuildGameHintPrompt(hq, ha, hb), llmCfg);
                                if (hr.ok && !hr.text.empty()) {
                                    std::ofstream hrf(hintResp);
                                    hrf << hr.text;
                                }
                            }
                        }
                        if (fs::exists(wantFile)) {
                            try { fs::remove(wantFile); } catch (...) {}

                            LLMResult r = InvokeLLM(
                                BuildGameSeriesPrompt(question, explanation, 4), llmCfg);
                            if (r.ok) {
                                auto bl = ParseMultipleGameChoices(r.text);
                                std::vector<GameData> batch;
                                for (auto& b : bl) {
                                    bool cia = (std::rand() % 2 == 0);
                                    GameData gd;
                                    gd.question   = b.question.empty() ? question : b.question;
                                    gd.choiceA    = cia ? b.correct : b.wrong;
                                    gd.choiceB    = cia ? b.wrong   : b.correct;
                                    gd.correctIsA = cia;
                                    batch.push_back(gd);
                                }
                                if (!batch.empty()) AppendGameFiles(tmpStr, batch);
                            }
                        }
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                    }
                }).detach();
            });
        }).detach();
        return;
    }

    if (url.StartsWith("testtaker://more/") || url.StartsWith("testtaker://less/")) {
        evt.Veto();
        bool isMore = url.StartsWith("testtaker://more/");
        long idx = -1;
        url.Mid(isMore ? 17 : 17).ToLong(&idx);  // both prefixes are 17 chars
        if (idx < 0 || idx >= (long)m_turns.size() || m_projectDir.empty()) return;

        std::string snippet = QuestionSnippet(m_turns[idx].question);
        if (isMore) {
            // Remove from lessOf if present, add to moreOf
            auto& less = m_cfg.lessOfTopics;
            less.erase(std::remove(less.begin(), less.end(), snippet), less.end());
            AddToTopicList(m_cfg.moreOfTopics, snippet);
        } else {
            // Remove from moreOf if present, add to lessOf
            auto& more = m_cfg.moreOfTopics;
            more.erase(std::remove(more.begin(), more.end(), snippet), more.end());
            AddToTopicList(m_cfg.lessOfTopics, snippet);
        }
        ProjectConfig pcfg = LoadConfig(m_projectDir);
        pcfg.examMoreOf = JoinPipeVec(m_cfg.moreOfTopics);
        pcfg.examLessOf = JoinPipeVec(m_cfg.lessOfTopics);
        SaveConfig(m_projectDir, pcfg);

        // Update both buttons in-place — no scroll reset.
        std::string si = std::to_string(idx);
        std::string script =
            "var m=document.querySelector(\"a[href='testtaker://more/" + si + "']\");"
            "var l=document.querySelector(\"a[href='testtaker://less/" + si + "']\");"
            "if(m){m.classList." + std::string(isMore ? "add" : "remove") + "('voted');}"
            "if(l){l.classList." + std::string(isMore ? "remove" : "add") + "('voted');}";
        m_webView->RunScript(script);
        return;
    }

    evt.Skip();
}
