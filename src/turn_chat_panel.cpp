#include "turn_chat_panel.h"
#include "turn_chat.h"
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
                              const LLMConfig&    llmCfg,
                              const std::string&  starterMessage,
                              const std::string&  starterDisplayQ) {
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

    std::string label = "Q" + std::to_string(turnIndex + 1)
                      + " \xe2\x80\x94 " + ScoreLabel(turn.score);
    m_titleLabel->SetLabel(wxString::FromUTF8(label));

    // Check whether this starter has already been asked in a previous session.
    auto starterAlreadyFired = [&](const std::string& displayQ) {
        for (const auto& t : m_turns)
            if (t.question == displayQ) return true;
        return false;
    };

    Logger::get().log("OpenTurn  idx=" + std::to_string(turnIndex)
                      + "  existingChatTurns=" + std::to_string(m_turns.size())
                      + "  hasStarter=" + std::to_string(!starterMessage.empty()));
    std::string displayQ = starterMessage.empty() ? ""
        : (starterDisplayQ.empty()
            ? "\xf0\x9f\x90\x92\xf0\x9f\x8d\x8c Explain with monkeys & bananas"
            : starterDisplayQ);

    if (!starterMessage.empty() && !starterAlreadyFired(displayQ)) {
        // Auto-fire the starter prompt — same flow as OnSend but with a synthetic question.
        m_busy = true;
        m_sendBtn->Enable(false);
        Logger::get().log("OpenTurn firing starter: " + displayQ);
        Render(displayQ);

        QuestionTurn      examTurn    = m_examTurn;
        std::string       sf         = m_sessionFile;
        int               ti         = m_turnIndex;
        LLMConfig         cfg        = m_llmCfg;

        std::thread([this, starterMessage, examTurn, sf, ti, cfg, displayQ]() {
            LLMResult res = InvokeLLM(starterMessage, cfg);
            wxTheApp->CallAfter([this, res, sf, ti, displayQ]() {
                m_busy = false;
                m_sendBtn->Enable(true);
                std::string answer = res.ok ? res.text : ("Error: " + res.error);
                TurnChatTurn t{displayQ, answer};
                m_turns.push_back(t);
                AppendTurnChatTurn(sf, ti, t);
                Render();
            });
        }).detach();
    } else {
        if (!starterMessage.empty())
            Logger::get().log("OpenTurn SKIP starter (already in history): " + displayQ);
        Render();
    }
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
    return BuildTurnChatHTML(m_examTurn, m_turnIndex, m_turns,
                             m_darkMode, m_savedIndices, pendingQ);
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
