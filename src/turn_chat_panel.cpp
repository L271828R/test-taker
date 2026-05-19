#include "turn_chat_panel.h"
#include "markdown.h"
#include "html_template.h"
#include "logger.h"
#include "saved_convos.h"
#include <ctime>
#include <filesystem>
#include <sstream>
#include <thread>
#include <wx/app.h>

namespace fs = std::filesystem;

enum { ID_TC_SEND = wxID_HIGHEST + 200, ID_TC_CLOSE };

wxBEGIN_EVENT_TABLE(TurnChatPanel, wxPanel)
    EVT_BUTTON(ID_TC_SEND,  TurnChatPanel::OnSend)
    EVT_BUTTON(ID_TC_CLOSE, TurnChatPanel::OnClose)
    EVT_WEBVIEW_NAVIGATING(wxID_ANY, TurnChatPanel::OnWebViewNav)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
TurnChatPanel::TurnChatPanel(wxWindow* parent, bool darkMode,
                              std::function<void()> onClose,
                              SavedConvoCallback    onSavedConvo)
    : wxPanel(parent), m_darkMode(darkMode),
      m_onClose(std::move(onClose)), m_onSavedConvo(std::move(onSavedConvo))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    // ── Header row: title + close button ─────────────────────────────────
    auto* headerRow = new wxBoxSizer(wxHORIZONTAL);
    m_titleLabel = new wxStaticText(this, wxID_ANY, "Follow-up discussion");
    wxFont boldFont = m_titleLabel->GetFont();
    boldFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_titleLabel->SetFont(boldFont);
    m_closeBtn = new wxButton(this, ID_TC_CLOSE, "\xc3\x97",
                              wxDefaultPosition, wxSize(28, 28), wxBU_EXACTFIT);
    m_closeBtn->SetToolTip("Close discussion panel");
    headerRow->Add(m_titleLabel, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    headerRow->Add(m_closeBtn,   0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    outer->Add(headerRow, 0, wxEXPAND | wxTOP, 4);

    m_webView = wxWebView::New(this, wxID_ANY, "about:blank");
    outer->Add(m_webView, 1, wxEXPAND);

    auto* inputRow = new wxBoxSizer(wxHORIZONTAL);
    m_inputCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                 wxDefaultPosition, wxSize(-1, 60),
                                 wxTE_MULTILINE);
    m_sendBtn = new wxButton(this, ID_TC_SEND, "Ask");

    if (m_darkMode) {
        wxColour bg(28, 33, 40);
        wxColour fg(230, 237, 243);
        m_inputCtrl->SetBackgroundColour(bg);
        m_inputCtrl->SetForegroundColour(fg);
        SetBackgroundColour(wxColour(13, 17, 23));
    }

    inputRow->Add(m_inputCtrl, 1, wxEXPAND | wxALL, 6);
    inputRow->Add(m_sendBtn, 0, wxALIGN_BOTTOM | wxALL, 6);
    outer->Add(inputRow, 0, wxEXPAND);

    m_sendBtn->Disable();
    SetSizer(outer);
    Render();
}

// ---------------------------------------------------------------------------
void TurnChatPanel::OpenTurn(const QuestionTurn& turn,
                              int                 turnIndex,
                              const std::string&  sessionFile,
                              const LLMConfig&    llmCfg) {
    m_examTurn     = turn;
    m_turnIndex    = turnIndex;
    m_sessionFile  = sessionFile;
    m_projectDir   = fs::path(sessionFile).parent_path().string();
    m_llmCfg       = llmCfg;
    m_busy         = false;
    m_savedIndices.clear();
    m_turns        = LoadTurnChat(sessionFile, turnIndex);
    m_sendBtn->Enable();
    m_inputCtrl->Clear();

    // Compact header: "Q1 — Partial" — the question is visible on the left
    std::string label = "Q" + std::to_string(turnIndex + 1)
                      + " \xe2\x80\x94 " + ScoreLabel(turn.score);
    m_titleLabel->SetLabel(wxString::FromUTF8(label));

    Render();
}

// ---------------------------------------------------------------------------
void TurnChatPanel::SetDarkMode(bool dark) {
    m_darkMode = dark;
    if (dark) {
        m_inputCtrl->SetBackgroundColour(wxColour(28, 33, 40));
        m_inputCtrl->SetForegroundColour(wxColour(230, 237, 243));
        SetBackgroundColour(wxColour(13, 17, 23));
    } else {
        m_inputCtrl->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        m_inputCtrl->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    }
    m_inputCtrl->Refresh();
    Refresh();
    Render();
}

void TurnChatPanel::Reset() {
    m_turnIndex   = -1;
    m_sessionFile.clear();
    m_turns.clear();
    m_examTurn    = {};
    m_busy        = false;
    m_savedIndices.clear();
    m_sendBtn->Disable();
    m_titleLabel->SetLabel("Follow-up discussion");
    Render();
}

// ---------------------------------------------------------------------------
void TurnChatPanel::OnClose(wxCommandEvent&) {
    if (m_onClose) m_onClose();
}

// ---------------------------------------------------------------------------
std::string TurnChatPanel::BuildChatHTML(const std::string& pendingQ) const {
    std::string scoreColour;
    switch (m_examTurn.score) {
        case Score::Star5:   scoreColour = "#1a7f37"; break;
        case Score::Star4:   scoreColour = "#2da44e"; break;
        case Score::Star3:   scoreColour = "#9a6700"; break;
        case Score::Star2:   scoreColour = "#e36b0a"; break;
        case Score::Star1:   scoreColour = "#cf222e"; break;
        default:             scoreColour = "#57606a"; break;
    }

    std::ostringstream body;

    // Extra CSS for the chat bubbles, layered on top of BuildHTML's theme tokens.
    body << R"(<style>
body { padding: 12px; }
.ctx { border-radius:6px; padding:8px 10px; margin-bottom:14px;
       border-left:3px solid )" << scoreColour << R"(;
       background:var(--surface); font-size:.88em; }
.ctx-score { display:inline-block; padding:.1em .45em; border-radius:4px;
             color:#fff; font-size:.85em; font-weight:600; margin-right:6px;
             vertical-align:middle; background:)" << scoreColour << R"(; }
.ctx-expl { color:var(--text-muted); vertical-align:middle; }
.turn { margin-bottom:16px; }
.turn-toolbar { display:flex; gap:0.4em; margin-bottom:0.3em; }
.turn:hover .tc-save-btn { opacity:1; }
.tc-save-btn { opacity:0; transition:opacity 0.15s;
  background:none; border:1px solid var(--border); border-radius:4px;
  padding:0.15em 0.5em; font-size:0.82em; cursor:pointer;
  color:var(--text-muted); text-decoration:none; white-space:nowrap; }
.tc-save-btn.saved { color:#1a7f37; border-color:#1a7f37; opacity:1; }
.q { background:var(--surface); border:1px solid var(--border);
     border-radius:8px 8px 8px 2px; padding:8px 12px;
     margin-bottom:6px; font-weight:500; }
.a { border-radius:2px 8px 8px 8px; padding:8px 12px; }
.a p:last-child { margin-bottom:0; }
.a pre { font-size:85%; }
.thinking { color:var(--text-muted); font-style:italic; padding:8px 12px; }
.empty { color:var(--text-muted); font-style:italic; }
</style>
)";

    if (m_turnIndex < 0) {
        body << "<p class='empty'>Click <strong>&#x1F4AC; discuss</strong> on any "
                "completed question to start a follow-up conversation.</p>";
    } else {
        // Compact explanation strip — question is visible on the left
        body << "<div class='ctx'>"
             << "<span class='ctx-score'>" << ScoreLabel(m_examTurn.score) << "</span>"
             << "<span class='ctx-expl'>" << EscapeHTML(m_examTurn.explanation) << "</span>"
             << "</div>";

        for (int i = 0; i < (int)m_turns.size(); ++i) {
            const auto& t = m_turns[i];
            bool isSaved = m_savedIndices.count(i) > 0;
            std::string saveClass = isSaved ? " saved" : "";
            std::string saveLabel = isSaved ? "&#x1F516; saved" : "&#x1F516; save";
            body << "<div class='turn'>"
                 << "<div class='turn-toolbar'>"
                 << "<a class='tc-save-btn" << saveClass << "' href='testtaker://tc-save/"
                 << i << "'>" << saveLabel << "</a>"
                 << "</div>"
                 << "<div class='q'>" << RenderMarkdown(t.question) << "</div>"
                 << "<div class='a'>" << RenderMarkdown(t.answer) << "</div>"
                 << "</div>\n";
        }
        if (!pendingQ.empty()) {
            body << "<div class='turn'>"
                 << "<div class='q'>" << RenderMarkdown(pendingQ) << "</div>"
                 << "<div class='thinking'>&#x22EF;</div>"
                 << "</div>\n";
        }
        if (m_turns.empty() && pendingQ.empty()) {
            body << "<p class='empty'>Ask a follow-up question about this result.</p>";
        }
    }

    body << "<script>window.scrollTo(0,document.body.scrollHeight);</script>";

    return BuildHTML(body.str(), "Discussion", m_darkMode);
}

// ---------------------------------------------------------------------------
void TurnChatPanel::Render(const std::string& pendingQ) {
    m_webView->SetPage(wxString::FromUTF8(BuildChatHTML(pendingQ)), wxEmptyString);
}

// ---------------------------------------------------------------------------
void TurnChatPanel::OnSend(wxCommandEvent&) {
    if (m_busy || m_turnIndex < 0) return;
    wxString raw = m_inputCtrl->GetValue().Trim();
    if (raw.empty()) return;

    std::string question = raw.ToStdString();
    m_inputCtrl->Clear();
    m_busy = true;
    m_sendBtn->Enable(false);
    Render(question);

    QuestionTurn      examTurn    = m_examTurn;
    std::vector<TurnChatTurn> history = m_turns;
    std::string       sessionFile = m_sessionFile;
    int               turnIndex   = m_turnIndex;
    LLMConfig         cfg         = m_llmCfg;
    std::string       corpusCtx   = CorpusContextFor(m_projectDir, question, cfg.ollamaUrl, "TurnChat",
                                                       CorpusTopK(cfg.backend));
    std::string       prompt      = BuildTurnChatPrompt(examTurn, history, question, corpusCtx);

    std::thread([this, prompt, cfg, sessionFile, turnIndex, question]() {
        LLMResult res = InvokeLLM(prompt, cfg);
        wxTheApp->CallAfter([this, res, sessionFile, turnIndex, question]() {
            m_busy = false;
            m_sendBtn->Enable(true);

            std::string answer = res.ok ? res.text : ("Error: " + res.error);
            if (answer.rfind("A: ", 0) == 0) answer = answer.substr(3);
            if (answer.rfind("A:", 0) == 0)  answer = answer.substr(2);
            while (!answer.empty() && (answer.front() == ' ' || answer.front() == '\n'))
                answer.erase(answer.begin());

            TurnChatTurn turn{question, answer};
            m_turns.push_back(turn);
            AppendTurnChatTurn(sessionFile, turnIndex, turn);
            Logger::get().log("TurnChat[" + std::to_string(turnIndex) + "] appended turn");
            Render();
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void TurnChatPanel::OnWebViewNav(wxWebViewEvent& evt) {
    wxString url = evt.GetURL();

    if (url.StartsWith("testtaker://tc-save/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(20).ToLong(&idx);  // "testtaker://tc-save/" is 20 chars
        if (idx < 0 || idx >= (long)m_turns.size()) return;
        if (m_savedIndices.count((int)idx)) return;  // already saved

        std::time_t now = std::time(nullptr);
        char dateBuf[16];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", std::localtime(&now));

        const auto& t = m_turns[idx];
        AppendSavedConvo(m_projectDir, t.question, t.answer, dateBuf);
        m_savedIndices.insert((int)idx);
        Logger::get().log("TurnChat turn " + std::to_string(idx) + " saved to saved_convos.md");

        if (m_onSavedConvo) m_onSavedConvo();
        Render();
        return;
    }

    evt.Skip();
}
