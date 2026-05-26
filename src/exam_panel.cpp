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
#include <wx/config.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>

namespace fs = std::filesystem;

wxBEGIN_EVENT_TABLE(ExamPanel, wxPanel)
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

    m_splitter = new wxSplitterWindow(this, wxID_ANY,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxSP_3D | wxSP_LIVE_UPDATE);
    m_splitter->SetMinimumPaneSize(200);

    m_webView = wxWebView::New(m_splitter, wxID_ANY, "about:blank");
    // Replace about:blank immediately with a tiny stub page that defines
    // setBusy/showHint. The bundled mermaid+highlight page from Render() is
    // ~20 MB and takes too long; without this, RunScript("setBusy(...)") fires
    // against about:blank (which has no JS) and shows an error dialog.
    m_webView->SetPage(
        "<html><head><script>"
        "function setBusy(m){}"
        "function showHint(t){}"
        "</script></head><body></body></html>", "");
    // Deferred: AddScriptMessageHandler internally calls RunScript which pumps
    // the event loop. Calling it here fires OnProjectActivated before sibling
    // panels (e.g. ReviewPanel) are constructed, causing a null-deref crash.
    wxTheApp->CallAfter([this]() {
        if (m_webView) m_webView->AddScriptMessageHandler("examAction");
    });
    m_webView->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
                    &ExamPanel::OnExamAction, this);

    m_chatPanel = new TurnChatPanel(m_splitter, m_darkMode,
        [this]() {
            m_chatOpen = false;
            if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);
            m_webView->RunScript(
                "var o=document.getElementById('chat-overlay');"
                "if(o)o.classList.remove('active');");
        },
        [this]() { if (m_onSavedConvo) m_onSavedConvo(); });

    m_splitter->Initialize(m_webView);

    outer->Add(m_splitter, 1, wxEXPAND);
    SetSizer(outer);
    // No Render() here — OnProjectActivated always calls either ResumeSession
    // or Clear(), both of which call Render(). The stub page above is enough.
}

// ---------------------------------------------------------------------------
void ExamPanel::SetDarkMode(bool dark) {
    m_darkMode = dark;
    m_chatPanel->SetDarkMode(dark);
    if (m_active) {
        Logger::get().log("ExamPanel::SetDarkMode triggering Render active=1"
                          "  busy=" + std::to_string(m_busy) +
                          "  hasQ=" + std::to_string(!m_currentQuestion.empty()));
        Render();
    }
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
    ApplyProjectExamConfig(LoadConfig(projectDir), m_cfg);
    m_thumbnails = LoadPersonalityThumbnails();
    Logger::get().log("ExamPanel::StartSession thumbnails=" + std::to_string(m_thumbnails.size())
        + "  personas=" + std::to_string(m_cfg.personalities.size()));
    for (const auto& kv : m_thumbnails)
        Logger::get().log("  thumb key: " + kv.first);
    m_active          = true;
    m_busy            = false;
    m_questionIndex   = 0;
    m_turns.clear();
    m_currentQuestion.clear();

    m_chatOpen = false;
    m_chatPanel->Reset();
    if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);

    Logger::get().log("ExamPanel::StartSession topic=" + cfg.topic
                      + "  total=" + std::to_string(cfg.totalQuestions));
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
    ApplyProjectExamConfig(LoadConfig(projectDir), m_cfg);

    // Session headers don't store personalities; load from the Personas tab config.
    if (m_cfg.personalities.empty()) {
        wxConfig cfg("TestTaker");
        cfg.SetPath("/charlib");
        wxString checked;
        cfg.Read("checked", &checked);
        wxStringTokenizer tok(checked, "|");
        while (tok.HasMoreTokens())
            m_cfg.personalities.push_back(tok.GetNextToken().ToStdString());
    }

    m_thumbnails = LoadPersonalityThumbnails();
    Logger::get().log("ExamPanel::ResumeSession thumbnails=" + std::to_string(m_thumbnails.size())
        + "  personas=" + std::to_string(m_cfg.personalities.size()));
    for (const auto& kv : m_thumbnails)
        Logger::get().log("  thumb key: " + kv.first);

    bool complete = (int)turns.size() >= hdr.totalQuestions;
    m_active = !complete;

    if (complete) {
        m_statusText = "Session complete. See Review tab for results.";
    } else {
        // Session was interrupted mid-way — ask next question.
        int next = (int)turns.size() + 1;
        m_statusText = "Resuming — question " + std::to_string(next)
                     + " of " + std::to_string(hdr.totalQuestions);
    }

    Logger::get().log("ExamPanel::ResumeSession"
                      "  turns=" + std::to_string(turns.size()) +
                      "  total=" + std::to_string(hdr.totalQuestions) +
                      "  complete=" + std::to_string(complete) +
                      "  m_busy=" + std::to_string(m_busy));

    // Render only — do NOT auto-fire an LLM call on resume.
    // The user clicks "Next question" when ready; this avoids an unwanted
    // LLM ping (and disabled buttons) every time the app opens or the tab is visited.
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

    m_statusText = "Drill: " + m_currentQuestion.substr(0, 60);
    Render();
}

// ---------------------------------------------------------------------------
void ExamPanel::RequestFirstQuestion() {
    if (m_busy) return;
    m_busy = true;
    Logger::get().log("ExamPanel::RequestFirstQuestion m_busy=true");
    m_webView->RunScript("if(typeof setBusy==='function')setBusy('Asking first question...')");

    ExamConfig  examCfg    = m_cfg;
    LLMConfig   llmCfg     = m_llmCfg;
    std::string projectDir = m_projectDir;

    std::thread([this, examCfg, llmCfg, projectDir]() {
        ExamConfig localCfg = examCfg;
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
            Logger::get().log("ExamPanel::RequestFirstQuestion callback m_busy=false ok="
                              + std::to_string(result.ok));
            if (!result.ok) {
                m_statusText = "LLM error: " + result.error;
                Logger::get().log("ExamPanel LLM error: " + result.error);
                Render();
                return;
            }
            m_currentQuestion = result.text;
            while (!m_currentQuestion.empty() &&
                   (m_currentQuestion.back() == '\n' || m_currentQuestion.back() == '\r' ||
                    m_currentQuestion.back() == ' '))
                m_currentQuestion.pop_back();

            m_hintText.clear();
            m_statusText = "Question 1 of " + std::to_string(m_cfg.totalQuestions);
            Render(true);
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void ExamPanel::RequestNextQuestion() {
    if (m_busy) return;
    m_busy = true;
    Logger::get().log("ExamPanel::RequestNextQuestion m_busy=true"
                      "  qIdx=" + std::to_string(m_questionIndex) +
                      "  turns=" + std::to_string(m_turns.size()));
    m_webView->RunScript("if(typeof setBusy==='function')setBusy('Asking next question...')");

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
            Logger::get().log("ExamPanel::RequestNextQuestion callback m_busy=false ok="
                              + std::to_string(result.ok));
            if (!result.ok) {
                m_statusText = "LLM error: " + result.error;
                Render();
                return;
            }
            auto scored = ParseScoredResponse(result.text);
            if (!scored.nextQuestion.empty()) {
                m_currentQuestion = scored.nextQuestion;
            } else {
                m_currentQuestion = result.text;
                while (!m_currentQuestion.empty() &&
                       (m_currentQuestion.back() == '\n' ||
                        m_currentQuestion.back() == ' '))
                    m_currentQuestion.pop_back();
            }
            m_hintText.clear();
            m_statusText = "Question " + std::to_string((int)m_turns.size() + 1)
                         + " of " + std::to_string(m_cfg.totalQuestions);
            Render(true);
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void ExamPanel::SubmitAnswer(const std::string& answer) {
    if (m_busy || m_currentQuestion.empty()) return;
    m_busy = true;
    m_webView->RunScript("if(typeof setBusy==='function')setBusy('Scoring...')");

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
            Logger::get().log("ExamPanel::SubmitAnswer callback m_busy=false ok="
                              + std::to_string(result.ok));
            if (!result.ok) {
                m_statusText = "LLM error: " + result.error;
                Logger::get().log("ExamPanel score error: " + result.error);
                Render();
                return;
            }

            auto scored = ParseScoredResponse(result.text);
            if (!scored.parseOk) {
                scored.score       = Score::Star1;
                scored.explanation = result.text;
                scored.parseOk     = true;
            }
            if (answer.empty()) scored.score = Score::Skipped;
            if (scored.explanation.empty()) scored.explanation = result.text;

            QuestionTurn turn;
            turn.question    = m_currentQuestion;
            turn.userAnswer  = answer;
            turn.score       = scored.score;
            turn.explanation = scored.explanation;
            turn.flagged     = false;
            m_turns.push_back(turn);
            AppendSessionTurn(m_sessionFile, turn);
            {
                std::string tkey = TidbitPersonaKey(turn.explanation);
                Logger::get().log("turn tidbit key: '" + tkey + "'"
                    + "  in_map=" + std::to_string(m_thumbnails.count(tkey))
                    + "  expl_len=" + std::to_string(turn.explanation.size()));
            }
            ++m_questionIndex;

            if (!scored.nextQuestion.empty()) {
                m_currentQuestion = scored.nextQuestion;
                m_hintText.clear();
                m_statusText = "Question " + std::to_string((int)m_turns.size() + 1)
                             + " of " + std::to_string(m_cfg.totalQuestions);
            } else {
                m_active          = false;
                m_currentQuestion.clear();
                m_hintText.clear();
                m_statusText = "Session complete! See Review tab for results.";
                if (m_onComplete) m_onComplete(m_sessionFile);
            }
            Render(true);
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void ExamPanel::Render(bool scrollToBottom) {
    Logger::get().log("ExamPanel::Render"
        "  active=" + std::to_string(m_active) +
        "  busy="   + std::to_string(m_busy) +
        "  hasQ="   + std::to_string(!m_currentQuestion.empty()) +
        "  turns="  + std::to_string(m_turns.size()));
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
                                m_cfg.moreOfTopics, m_cfg.lessOfTopics,
                                m_thumbnails);

        body << BuildCurrentQuestionHTML(m_currentQuestion, m_busy);
        if (!m_active && !m_turns.empty()) {
            body << "<p class='done'>Session complete. Check the Review tab.</p>";
        }
    }

    std::string extraCSS = R"(<style>
@keyframes examPageIn{from{opacity:0}to{opacity:1}}
body{animation:examPageIn 0.3s ease-out;}
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
)"
        + PersonalityDropdownJS("game-drop", "explain-btn");

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

    ExamInputState inputState;
    inputState.active          = m_active;
    inputState.busy            = m_busy;
    inputState.hasQuestion     = !m_currentQuestion.empty();
    inputState.readyForNext    = m_active && !m_busy && m_currentQuestion.empty();
    inputState.canFlag         = !m_turns.empty();
    inputState.lastTurnFlagged = !m_turns.empty() && m_turns.back().flagged;
    inputState.hintText        = m_hintText;
    inputState.statusText      = m_statusText;

    std::string inputSection = BuildExamInputSection(inputState);

    // Wrap scrollable content in flex:1 so the body flex-column pushes
    // the input section to the viewport bottom without any position:fixed.
    std::string content = "<div style='flex:1'>"
                        + overlay + deepdiveBtn + body.str()
                        + "</div>";

    return BuildHTML(extraCSS + content + inputSection, "Exam", m_darkMode);
}

// ---------------------------------------------------------------------------
// JS → C++ bridge: all exam input actions arrive here.
// ---------------------------------------------------------------------------
static std::string examJsonField(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    std::string val;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < json.size()) {
            char e = json[pos++];
            switch (e) {
                case '"':  val += '"';  break;
                case '\\': val += '\\'; break;
                case 'n':  val += '\n'; break;
                case 'r':  val += '\r'; break;
                case 't':  val += '\t'; break;
                default:   val += e;   break;
            }
        } else val += c;
    }
    return val;
}

static std::string escapeJS(const std::string& s) {
    std::string out;
    for (char c : s) {
        if      (c == '\\') out += "\\\\";
        else if (c == '\'') out += "\\'";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') ;
        else                out += c;
    }
    return out;
}

void ExamPanel::OnExamAction(wxWebViewEvent& evt) {
    std::string payload = evt.GetString().ToStdString();
    std::string action  = examJsonField(payload, "action");
    std::string answer  = examJsonField(payload, "answer");

    if (action == "nextQuestion") {
        RequestNextQuestion();

    } else if (action == "submit") {
        SubmitAnswer(answer);

    } else if (action == "skip") {
        SubmitAnswer("");

    } else if (action == "silentSkip") {
        if (m_busy || m_currentQuestion.empty()) return;

        QuestionTurn turn;
        turn.question   = m_currentQuestion;
        turn.score      = Score::Skipped;
        turn.silentSkip = true;
        m_turns.push_back(turn);
        AppendSessionTurn(m_sessionFile, turn);

        m_currentQuestion.clear();
        ++m_questionIndex;

        if (m_questionIndex < m_cfg.totalQuestions) {
            RequestNextQuestion();
        } else {
            m_active = false;
            m_statusText = "Session complete! See Review tab for results.";
            if (m_onComplete) m_onComplete(m_sessionFile);
            Render(true);
        }

    } else if (action == "hint") {
        if (m_busy || m_currentQuestion.empty()) return;
        m_webView->RunScript(
            "document.getElementById('btn-hint').disabled=true;"
            "document.getElementById('btn-hint').textContent='\xe2\x8f\xb3 ...';" );

        std::string question = m_currentQuestion;
        LLMConfig   llmCfg   = m_llmCfg;

        std::thread([=]() {
            LLMResult result = InvokeLLM(BuildHintPrompt(question), llmCfg);
            wxTheApp->CallAfter([=]() {
                std::string hint = result.ok ? result.text : "(could not fetch hint)";
                while (!hint.empty() && (hint.front() == '\n' || hint.front() == ' '))
                    hint.erase(hint.begin());
                while (!hint.empty() && (hint.back() == '\n' || hint.back() == ' '))
                    hint.pop_back();

                m_hintText = hint;
                m_webView->RunScript(
                    "document.getElementById('btn-hint').disabled=false;"
                    "document.getElementById('btn-hint').textContent='\xf0\x9f\x92\xa1 Hint';"
                    "showHint('" + escapeJS(ProcessInline(hint)) + "');" );
            });
        }).detach();

    } else if (action == "flag") {
        if (m_turns.empty()) return;
        int idx = (int)m_turns.size() - 1;
        m_turns[idx].flagged = !m_turns[idx].flagged;
        SetTurnFlagged(m_sessionFile, idx, m_turns[idx].flagged);
        Render();

    } else if (action == "abandon") {
        int ans = wxMessageBox(
            "End this session now?\n\nProgress so far will be saved to the Review tab.",
            "End Session", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
        if (ans != wxYES) return;
        AbandonSession();

    } else if (action == "focusInput") {
        if (m_splitter->IsSplit()) {
            m_splitter->Unsplit(m_chatPanel);
            m_chatOpen = false;
            m_webView->RunScript(
                "var o=document.getElementById('chat-overlay');"
                "if(o)o.classList.remove('active');");
        }
    }
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

void ExamPanel::Clear() {
    m_active          = false;
    m_busy            = false;
    m_turns.clear();
    m_historyGroups.clear();
    m_currentQuestion.clear();
    m_sessionFile.clear();
    m_projectDir.clear();
    m_hintText.clear();
    m_statusText.clear();

    m_chatOpen = false;
    m_chatPanel->Reset();
    if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);

    Render();
}

void ExamPanel::AbandonSession() {
    m_active          = false;
    m_busy            = false;
    m_currentQuestion.clear();
    m_hintText.clear();
    m_statusText      = "Session ended. See Review tab for results.";

    if (!m_projectDir.empty()) {
        ProjectConfig pcfg = LoadConfig(m_projectDir);
        pcfg.lastSession.clear();
        SaveConfig(m_projectDir, pcfg);
    }

    if (m_onComplete) m_onComplete(m_sessionFile);
    Render();
    Logger::get().log("Session abandoned: " + m_sessionFile);
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
            m_splitter->SplitVertically(m_webView, m_chatPanel, w * 6 / 10);
        }
        m_chatOpen = true;
        m_webView->RunScript(
            "var o=document.getElementById('chat-overlay');"
            "if(o)o.classList.add('active');"
            "document.querySelectorAll('.turn.active').forEach(function(t){t.classList.remove('active');});"
            "var t=document.getElementById('turn-" + std::to_string(idx) + "');"
            "if(t)t.classList.add('active');");
        return;
    }

    if (url.StartsWith("testtaker://explain/")) {
        evt.Veto();
        // URL: testtaker://explain/{slug}/{idx}
        wxString rest = url.Mid(20);  // after "testtaker://explain/"
        int slash = rest.Find('/');
        if (slash < 0) return;
        std::string slug = rest.Left(slash).ToStdString();
        long idx = -1;
        rest.Mid(slash + 1).ToLong(&idx);
        if (idx < 0 || idx >= (long)m_turns.size()) return;
        const QuestionTurn& turn = m_turns[idx];
        if (turn.explanation.empty()) return;
        const PersonalityDef* def = FindPersonality(slug);
        if (!def) return;

        std::string starter = BuildPersonalityPrompt(*def, turn.question, turn.userAnswer, turn.explanation);
        m_chatPanel->OpenTurn(turn, (int)idx, m_sessionFile, m_llmCfg, starter, def->displayQ);
        if (!m_splitter->IsSplit()) {
            int w = m_splitter->GetClientSize().GetWidth();
            m_splitter->SplitVertically(m_webView, m_chatPanel, w * 6 / 10);
        }
        m_chatOpen = true;
        m_webView->RunScript(
            "var o=document.getElementById('chat-overlay');"
            "if(o)o.classList.add('active');"
            "document.querySelectorAll('.turn.active').forEach(function(t){t.classList.remove('active');});"
            "var t=document.getElementById('turn-" + std::to_string(idx) + "');"
            "if(t)t.classList.add('active');");
        return;
    }

    if (url.StartsWith("testtaker://whynot/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(19).ToLong(&idx);  // "testtaker://whynot/" is 19 chars
        if (idx < 0 || idx >= (long)m_turns.size()) return;
        const QuestionTurn& turn = m_turns[idx];
        if (turn.explanation.empty()) return;

        std::string starter = BuildWhyNotPerfectPrompt(
            turn.question, turn.userAnswer, turn.explanation, turn.score);
        m_chatPanel->OpenTurn(turn, (int)idx, m_sessionFile, m_llmCfg, starter);

        if (!m_splitter->IsSplit()) {
            int w = m_splitter->GetClientSize().GetWidth();
            m_splitter->SplitVertically(m_webView, m_chatPanel, w * 6 / 10);
        }
        m_chatOpen = true;
        m_webView->RunScript(
            "var o=document.getElementById('chat-overlay');"
            "if(o)o.classList.add('active');"
            "document.querySelectorAll('.turn.active').forEach(function(t){t.classList.remove('active');});"
            "var t=document.getElementById('turn-" + std::to_string(idx) + "');"
            "if(t)t.classList.add('active');");
        m_webView->RunScript(
            "var b=document.querySelector(\"a[href='testtaker://whynot/" +
            std::to_string(idx) + "']\");"
            "if(b)b.classList.add('open');");
        return;
    }

    if (url == "testtaker://closechat") {
        evt.Veto();
        m_chatOpen = false;
        if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);
        m_webView->RunScript(
            "var o=document.getElementById('chat-overlay');"
            "if(o)o.classList.remove('active');"
            "document.querySelectorAll('.turn.active').forEach(function(t){t.classList.remove('active');});");
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
            m_splitter->SplitVertically(m_webView, m_chatPanel, w * 6 / 10);
        }
        m_chatOpen = true;
        m_webView->RunScript(
            "var o=document.getElementById('chat-overlay');"
            "if(o)o.classList.add('active');");
        return;
    }

    if (url.StartsWith("testtaker://hexplain/")) {
        evt.Veto();
        // URL: testtaker://hexplain/{slug}/{g}/{i}
        wxString rest = url.Mid(21);  // after "testtaker://hexplain/"
        int slash = rest.Find('/');
        if (slash < 0) return;
        std::string slug = rest.Left(slash).ToStdString();
        long g = -1, i = -1;
        if (!parseHistIdx(rest.Mid(slash + 1), g, i)) return;
        if (g < 0 || g >= (long)m_historyGroups.size()) return;
        if (i < 0 || i >= (long)m_historyGroups[g].turns.size()) return;
        const auto& grp = m_historyGroups[g];
        const QuestionTurn& turn = grp.turns[i];
        if (turn.explanation.empty()) return;
        const PersonalityDef* def = FindPersonality(slug);
        if (!def) return;

        std::string starter = BuildPersonalityPrompt(*def, turn.question, turn.userAnswer, turn.explanation);
        m_chatPanel->OpenTurn(turn, (int)i, grp.sessionFile, m_llmCfg, starter, def->displayQ);
        if (!m_splitter->IsSplit()) {
            int w = m_splitter->GetClientSize().GetWidth();
            m_splitter->SplitVertically(m_webView, m_chatPanel, w * 6 / 10);
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

    if (url.StartsWith("testtaker://hrocks/")) {
        evt.Veto();
        long g = -1, i = -1;
        auto parseHistIdx2 = [](const wxString& rest, long& g, long& i) -> bool {
            int slash = rest.Find('/');
            if (slash < 0) return false;
            return rest.Left(slash).ToLong(&g) && rest.Mid(slash + 1).ToLong(&i);
        };
        if (!parseHistIdx2(url.Mid(19), g, i)) return;
        if (g < 0 || g >= (long)m_historyGroups.size()) return;
        if (i < 0 || i >= (long)m_historyGroups[g].turns.size()) return;
        const QuestionTurn& turn = m_historyGroups[g].turns[i];
        if (turn.explanation.empty()) return;

        std::string gStr = std::to_string(g), iStr = std::to_string(i);
        std::string href = "testtaker://hrocks/" + gStr + "/" + iStr;
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
                    "if(g){g.textContent='💫 rocks';g.style.pointerEvents='';}");

                auto b1 = ParseMultipleGameChoices(r1.text);
                auto b2 = ParseMultipleGameChoices(r2.text);
                if (b1.empty() && b2.empty()) { Logger::get().log("hrocks parse failed"); return; }

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

                wxString tmpPath = wxFileName::CreateTempFileName("tt-asteroids") + ".dat";
                if (!WriteGameFiles(tmpPath.ToStdString(), all)) {
                    Logger::get().log("hrocks: could not write game data file"); return;
                }
                wxFileName exeDir(wxStandardPaths::Get().GetExecutablePath());
                wxString gameBin = exeDir.GetPath() + "/test-taker-asteroids";
                if (!wxFileExists(gameBin))
                    gameBin = exeDir.GetPath() + "/../test-taker-asteroids";
                if (!wxFileExists(gameBin)) {
                    Logger::get().log("test-taker-asteroids binary not found"); return;
                }
                wxExecute("\"" + gameBin + "\" \"" + tmpPath + "\"", wxEXEC_ASYNC);
                Logger::get().log("hrocks: launched with " + std::to_string(all.size()) + " questions");

                std::string tmpStr   = tmpPath.ToStdString();
                std::string wantFile = tmpStr + ".want";
                std::string saveFile = tmpStr + ".save";
                std::string hintReq  = tmpStr + ".hintreq";
                std::string hintResp = tmpStr + ".hintresp";
                std::string pDir     = m_projectDir;
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

    if (url.StartsWith("testtaker://rocks/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(18).ToLong(&idx);  // "testtaker://rocks/" is 18 chars
        if (idx < 0 || idx >= (long)m_turns.size()) return;
        const QuestionTurn& turn = m_turns[idx];
        if (turn.explanation.empty()) return;

        std::string si = std::to_string(idx);
        m_webView->RunScript(
            "var g=document.querySelector(\"a[href='testtaker://rocks/" + si + "']\");"
            "if(g){g.textContent='⏳ generating…';g.style.pointerEvents='none';}");

        std::string question   = turn.question;
        std::string explanation = turn.explanation;
        std::string projectDir  = m_projectDir;
        LLMConfig   llmCfg     = m_llmCfg;

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
                    "var g=document.querySelector(\"a[href='testtaker://rocks/" + si + "']\");"
                    "if(g){g.textContent='💫 Asteroids';g.style.pointerEvents='';}");

                auto b1 = ParseMultipleGameChoices(r1.text);
                auto b2 = ParseMultipleGameChoices(r2.text);
                if (b1.empty() && b2.empty()) {
                    Logger::get().log("Asteroids series parse failed"); return;
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

                wxString tmpPath = wxFileName::CreateTempFileName("tt-asteroids") + ".dat";
                if (!WriteGameFiles(tmpPath.ToStdString(), all)) {
                    Logger::get().log("Could not write asteroids data file"); return;
                }
                wxFileName exeDir(wxStandardPaths::Get().GetExecutablePath());
                wxString gameBin = exeDir.GetPath() + "/test-taker-asteroids";
                if (!wxFileExists(gameBin))
                    gameBin = exeDir.GetPath() + "/../test-taker-asteroids";
                if (!wxFileExists(gameBin)) {
                    Logger::get().log("test-taker-asteroids binary not found"); return;
                }
                wxExecute("\"" + gameBin + "\" \"" + tmpPath + "\"", wxEXEC_ASYNC);
                Logger::get().log("Launched asteroids with " + std::to_string(all.size()) + " questions");

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
                            { std::ifstream sf(saveFile); std::string ln;
                              while (std::getline(sf, ln)) {
                                  if (ln.rfind("Q: ",0)==0) q = ln.substr(3);
                                  if (ln.rfind("A: ",0)==0) a = ln.substr(3);
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
                            { std::ifstream hf(hintReq); std::string ln;
                              while (std::getline(hf, ln)) {
                                  if (ln.rfind("Q: ",0)==0) hq = ln.substr(3);
                                  if (ln.rfind("A: ",0)==0) ha = ln.substr(3);
                                  if (ln.rfind("B: ",0)==0) hb = ln.substr(3);
                              } }
                            try { fs::remove(hintReq); } catch (...) {}
                            if (!hq.empty()) {
                                LLMResult hr = InvokeLLM(
                                    BuildGameHintPrompt(hq, ha, hb), llmCfg);
                                if (hr.ok && !hr.text.empty()) {
                                    std::ofstream hrf(hintResp); hrf << hr.text;
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
