#include "exam_panel.h"
#include "html_template.h"
#include "markdown.h"
#include "logger.h"
#include <sstream>
#include <thread>
#include <wx/app.h>

enum {
    ID_EXAM_SEND = wxID_HIGHEST + 100,
    ID_EXAM_SKIP,
    ID_EXAM_FLAG,
};

wxBEGIN_EVENT_TABLE(ExamPanel, wxPanel)
    EVT_BUTTON(ID_EXAM_SEND, ExamPanel::OnSend)
    EVT_BUTTON(ID_EXAM_SKIP, ExamPanel::OnSkip)
    EVT_BUTTON(ID_EXAM_FLAG, ExamPanel::OnFlag)
wxEND_EVENT_TABLE()

ExamPanel::ExamPanel(wxWindow* parent, SessionCompleteCallback onSessionComplete)
    : wxPanel(parent), m_onComplete(std::move(onSessionComplete))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    m_webView = wxWebView::New(this, wxID_ANY, "about:blank");
    outer->Add(m_webView, 1, wxEXPAND);

    // ── Answer input row ──────────────────────────────────────────────────
    auto* inputRow = new wxBoxSizer(wxHORIZONTAL);
    m_answerCtrl = new wxTextCtrl(this, wxID_ANY, "",
        wxDefaultPosition, wxSize(-1, 80), wxTE_MULTILINE | wxTE_PROCESS_ENTER);
    m_sendBtn = new wxButton(this, ID_EXAM_SEND, "Submit");
    m_skipBtn = new wxButton(this, ID_EXAM_SKIP, "I don't know");
    m_flagBtn = new wxButton(this, ID_EXAM_FLAG, "Flag for review");

    inputRow->Add(m_answerCtrl, 1, wxEXPAND | wxRIGHT, 4);
    auto* btnCol = new wxBoxSizer(wxVERTICAL);
    btnCol->Add(m_sendBtn, 0, wxEXPAND | wxBOTTOM, 4);
    btnCol->Add(m_skipBtn, 0, wxEXPAND | wxBOTTOM, 4);
    btnCol->Add(m_flagBtn, 0, wxEXPAND);
    inputRow->Add(btnCol, 0, wxEXPAND);

    m_statusLabel = new wxStaticText(this, wxID_ANY, "");

    outer->Add(inputRow, 0, wxEXPAND | wxALL, 6);
    outer->Add(m_statusLabel, 0, wxLEFT | wxBOTTOM, 8);

    SetSizer(outer);

    // Start idle — no active session yet.
    m_sendBtn->Disable();
    m_skipBtn->Disable();
    m_flagBtn->Disable();
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

    RequestFirstQuestion();
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
                // Session complete
                m_active          = false;
                m_currentQuestion.clear();
                m_statusLabel->SetLabel("Session complete! See Review tab for results.");
                m_sendBtn->Disable();
                m_skipBtn->Disable();
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
        // Render completed turns
        for (const auto& t : m_turns) {
            std::string scoreClass =
                t.score == Score::Correct ? "correct" :
                t.score == Score::Partial ? "partial" :
                t.score == Score::Missed  ? "missed"  : "skipped";
            body << "<div class='turn'>"
                 << "<div class='question'>"
                 << RenderMarkdown(t.question) << "</div>"
                 << "<div class='answer'><strong>Your answer:</strong> "
                 << EscapeHTML(t.userAnswer.empty() ? "(skipped)" : t.userAnswer)
                 << "</div>"
                 << "<div class='verdict " << scoreClass << "'>"
                 << ScoreLabel(t.score) << "</div>"
                 << "<div class='explanation'>"
                 << RenderMarkdown(t.explanation) << "</div>"
                 << "</div>";
        }

        // Current question
        if (!m_currentQuestion.empty()) {
            body << "<div class='current-question'>"
                 << RenderMarkdown(m_currentQuestion)
                 << "</div>";
        }

        if (!m_active && !m_turns.empty()) {
            body << "<p class='done'>Session complete. Check the Review tab.</p>";
        }
    }

    // Prepend exam-specific CSS as an inline <style> block inside the body.
    std::string examCSS = R"(<style>
.turn { border-bottom:1px solid var(--border); margin-bottom:1.2em; padding-bottom:1em; }
.question { font-weight:600; margin-bottom:.4em; }
.answer { color:var(--text-muted); margin-bottom:.3em; font-style:italic; }
.verdict { display:inline-block; padding:.15em .6em; border-radius:4px;
           font-size:.85em; font-weight:600; margin-bottom:.4em; }
.verdict.correct { background:#1a7f37; color:#fff; }
.verdict.partial  { background:#9a6700; color:#fff; }
.verdict.missed   { background:#cf222e; color:#fff; }
.verdict.skipped  { background:#57606a; color:#fff; }
.explanation { font-size:.95em; }
.current-question { background:var(--surface); border:2px solid var(--link);
                    border-radius:6px; padding:1em; margin-top:1.5em;
                    font-size:1.05em; }
.done { color:var(--text-muted); margin-top:1.5em; }
</style>
)";
    return BuildHTML(examCSS + body.str(), "Exam", m_darkMode);
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

void ExamPanel::OnFlag(wxCommandEvent&) {
    if (m_turns.empty()) return;
    int idx = (int)m_turns.size() - 1;
    m_turns[idx].flagged = !m_turns[idx].flagged;
    SetTurnFlagged(m_sessionFile, idx, m_turns[idx].flagged);
    m_flagBtn->SetLabel(m_turns[idx].flagged ? "Unflag" : "Flag for review");
}
