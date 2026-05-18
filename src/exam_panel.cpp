#include "exam_panel.h"
#include "html_template.h"
#include "exam_prompt.h"
#include "markdown.h"
#include "logger.h"
#include <sstream>
#include <thread>
#include <wx/app.h>

enum {
    ID_EXAM_SEND = wxID_HIGHEST + 100,
    ID_EXAM_SKIP,
    ID_EXAM_FLAG,
    ID_EXAM_ABANDON,
};

wxBEGIN_EVENT_TABLE(ExamPanel, wxPanel)
    EVT_BUTTON(ID_EXAM_SEND,    ExamPanel::OnSend)
    EVT_BUTTON(ID_EXAM_SKIP,    ExamPanel::OnSkip)
    EVT_BUTTON(ID_EXAM_FLAG,    ExamPanel::OnFlag)
    EVT_BUTTON(ID_EXAM_ABANDON, ExamPanel::OnAbandon)
    EVT_WEBVIEW_NAVIGATING(wxID_ANY, ExamPanel::OnWebViewNav)
wxEND_EVENT_TABLE()

ExamPanel::ExamPanel(wxWindow* parent, SessionCompleteCallback onSessionComplete)
    : wxPanel(parent), m_onComplete(std::move(onSessionComplete))
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
        if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);
        evt.Skip();
    });
    leftSizer->Add(m_webView, 1, wxEXPAND);

    // ── Answer input row ──────────────────────────────────────────────────
    auto* inputRow = new wxBoxSizer(wxHORIZONTAL);
    m_answerCtrl = new wxTextCtrl(m_leftPanel, wxID_ANY, "",
        wxDefaultPosition, wxSize(-1, 80), wxTE_MULTILINE | wxTE_PROCESS_ENTER);
    m_answerCtrl->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& evt) {
        if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);
        evt.Skip();
    });
    m_sendBtn    = new wxButton(m_leftPanel, ID_EXAM_SEND,    "Submit");
    m_skipBtn    = new wxButton(m_leftPanel, ID_EXAM_SKIP,    "I don't know");
    m_flagBtn    = new wxButton(m_leftPanel, ID_EXAM_FLAG,    "Flag for review");
    m_abandonBtn = new wxButton(m_leftPanel, ID_EXAM_ABANDON, "End Session");

    inputRow->Add(m_answerCtrl, 1, wxEXPAND | wxRIGHT, 4);
    auto* btnCol = new wxBoxSizer(wxVERTICAL);
    btnCol->Add(m_sendBtn,    0, wxEXPAND | wxBOTTOM, 4);
    btnCol->Add(m_skipBtn,    0, wxEXPAND | wxBOTTOM, 4);
    btnCol->Add(m_flagBtn,    0, wxEXPAND | wxBOTTOM, 4);
    btnCol->Add(m_abandonBtn, 0, wxEXPAND);
    inputRow->Add(btnCol, 0, wxEXPAND);

    m_statusLabel = new wxStaticText(m_leftPanel, wxID_ANY, "");

    leftSizer->Add(inputRow, 0, wxEXPAND | wxALL, 6);
    leftSizer->Add(m_statusLabel, 0, wxLEFT | wxBOTTOM, 8);
    m_leftPanel->SetSizer(leftSizer);

    m_chatPanel = new TurnChatPanel(m_splitter, m_darkMode, [this]() {
        m_chatOpen = false;
        if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);
        Render();
    });

    // Start unsplit — chat opens when the user clicks Discuss
    m_splitter->Initialize(m_leftPanel);

    outer->Add(m_splitter, 1, wxEXPAND);
    SetSizer(outer);

    // Start idle — no active session yet.
    m_sendBtn->Disable();
    m_skipBtn->Disable();
    m_flagBtn->Disable();
    m_abandonBtn->Disable();
    Render();
}

// ---------------------------------------------------------------------------
void ExamPanel::StartSession(const std::string& projectDir,
                             const std::string& sessionFile,
                             const ExamConfig&  cfg,
                             const LLMConfig&   llmCfg,
                             bool               darkMode) {
    m_projectDir      = projectDir;
    m_sessionFile     = sessionFile;
    m_cfg             = cfg;
    m_llmCfg          = llmCfg;
    m_darkMode        = darkMode;
    m_active          = true;
    m_busy            = false;
    m_questionIndex   = 0;
    m_turns.clear();
    m_currentQuestion.clear();

    m_sendBtn->Enable();
    m_skipBtn->Enable();
    m_flagBtn->Disable(); // enabled after first question is answered
    m_abandonBtn->Enable();

    m_chatOpen = false;
    m_chatPanel->Reset();
    if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);

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
    m_darkMode        = darkMode;
    m_turns           = turns;
    m_questionIndex   = (int)turns.size();
    m_currentQuestion.clear();

    m_cfg.topic          = hdr.topic;
    m_cfg.instructions   = hdr.instructions;
    m_cfg.difficulty     = hdr.difficulty;
    m_cfg.totalQuestions = hdr.totalQuestions;
    m_cfg.projectContext.clear();

    bool complete = (int)turns.size() >= hdr.totalQuestions;
    m_active = !complete;

    if (complete) {
        m_sendBtn->Disable();
        m_skipBtn->Disable();
        m_flagBtn->Enable(!turns.empty());
        m_abandonBtn->Disable();
        m_statusLabel->SetLabel("Session complete. See Review tab for results.");
    } else {
        // Session was interrupted mid-way — re-enable input, ask next question.
        m_sendBtn->Enable();
        m_skipBtn->Enable();
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
    m_darkMode      = darkMode;
    m_active        = true;
    m_busy          = false;
    m_turns         = {};
    m_questionIndex = 0;

    // Pre-load the flagged question as the current question.
    m_currentQuestion = turns[questionIndex].question;
    m_cfg.totalQuestions = 1;

    m_sendBtn->Enable();
    m_skipBtn->Enable();
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
    m_skipBtn->Disable();

    std::string prompt = BuildFirstQuestionPrompt(m_cfg);
    LLMConfig   cfg    = m_llmCfg;

    std::thread([this, prompt, cfg]() {
        auto result = InvokeLLM(prompt, cfg);
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
            m_skipBtn->Enable();
            Render();
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void ExamPanel::RequestNextQuestion() {
    if (m_busy) return;
    m_busy = true;
    m_sendBtn->Disable();
    m_skipBtn->Disable();
    m_statusLabel->SetLabel("Asking next question…");

    int remaining = m_cfg.totalQuestions - (int)m_turns.size() - 1;
    // Build a scoring prompt with a placeholder "resumed" answer so the LLM
    // just emits NEXT_QUESTION without re-scoring anything.
    std::string prompt = BuildScoringAndNextPrompt(
        m_cfg, m_turns, "(session resumed — generate next question only)",
        "(resumed)", remaining);
    LLMConfig cfg = m_llmCfg;

    std::thread([this, prompt, cfg]() {
        auto result = InvokeLLM(prompt, cfg);
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
            m_skipBtn->Enable();
            Render();
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void ExamPanel::SubmitAnswer(const std::string& answer) {
    if (m_busy || m_currentQuestion.empty()) return;
    m_busy = true;
    m_sendBtn->Disable();
    m_skipBtn->Disable();
    m_statusLabel->SetLabel("Scoring…");

    int remaining = m_cfg.totalQuestions - (int)m_turns.size() - 1;
    std::string prompt = BuildScoringAndNextPrompt(
        m_cfg, m_turns, m_currentQuestion, answer, remaining);
    LLMConfig cfg = m_llmCfg;

    std::thread([this, answer, prompt, cfg, remaining]() {
        auto result = InvokeLLM(prompt, cfg);
        wxTheApp->CallAfter([this, answer, result, remaining]() {
            m_busy = false;
            if (!result.ok) {
                m_statusLabel->SetLabel("LLM error: " + result.error);
                Logger::get().log("ExamPanel score error: " + result.error);
                m_sendBtn->Enable();
                m_skipBtn->Enable();
                return;
            }

            auto scored = ParseScoredResponse(result.text);
            if (!scored.parseOk) {
                // Graceful fallback: treat whole response as explanation
                scored.score       = answer.empty() ? Score::Skipped : Score::Missed;
                scored.explanation = result.text;
                scored.parseOk     = true;
            }

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
                m_skipBtn->Enable();
                m_answerCtrl->Clear();
            } else {
                // Session complete — keep lastSessionFile so Exam tab can show
                // the completed turns read-only on next startup.
                m_active          = false;
                m_currentQuestion.clear();
                m_statusLabel->SetLabel("Session complete! See Review tab for results.");
                m_sendBtn->Disable();
                m_skipBtn->Disable();
                m_abandonBtn->Disable();
                if (m_onComplete) m_onComplete(m_sessionFile);
            }
            Render();
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void ExamPanel::Render() {
    std::string html = BuildExamHTML();
    m_webView->SetPage(wxString::FromUTF8(html), "");
}

// ---------------------------------------------------------------------------
std::string ExamPanel::BuildExamHTML() const {
    std::ostringstream body;

    if (!m_active && m_turns.empty()) {
        body << "<p style='color:var(--text-muted);margin-top:2em'>"
                "No active session. Go to <strong>New Session</strong> to start.</p>";
    } else {
        // Load chat counts for each completed turn so the discuss button can
        // show a badge when a turn already has follow-up exchanges.
        std::vector<int> chatCounts;
        chatCounts.reserve(m_turns.size());
        for (int i = 0; i < (int)m_turns.size(); ++i)
            chatCounts.push_back((int)LoadTurnChat(m_sessionFile, i).size());

        body << RenderExamTurns(m_turns, chatCounts);

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
</style>
)";

    std::string overlay;
    if (m_chatOpen)
        overlay = "<div id='chat-overlay' class='active' "
                  "onclick=\"window.location='testtaker://closechat'\"></div>";

    return BuildHTML(extraCSS + overlay + body.str(), "Exam", m_darkMode);
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
    m_currentQuestion.clear();
    m_sessionFile.clear();
    m_projectDir.clear();

    m_sendBtn->Disable();
    m_skipBtn->Disable();
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
    m_skipBtn->Disable();
    m_abandonBtn->Disable();
    m_statusLabel->SetLabel("Session ended. See Review tab for results.");

    AppState st = LoadAppState();
    st.lastSessionFile.clear();
    SaveAppState(st);

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
        Render();
        return;
    }

    if (url == "testtaker://closechat") {
        evt.Veto();
        m_chatOpen = false;
        if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);
        Render();
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

    evt.Skip();
}
