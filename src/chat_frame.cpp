#include "chat_frame.h"
#include "conversation.h"
#include "llm.h"
#include "markdown.h"
#include "config.h"
#include "meta.h"
#include <chrono>
#include <filesystem>
#include <thread>
#include <fstream>
#include <wx/sizer.h>
#include <wx/webview.h>

namespace fs = std::filesystem;

enum { ID_CF_SEND = wxID_HIGHEST + 500 };

wxBEGIN_EVENT_TABLE(ChatFrame, wxFrame)
    EVT_BUTTON(ID_CF_SEND, ChatFrame::OnSend)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
static std::string ChatHTML(const std::string& chTitle,
                            const std::vector<ConversationTurn>& turns,
                            const std::string& pendingQ,
                            bool darkMode) {
    const std::string bg      = darkMode ? "#0d1117" : "#ffffff";
    const std::string text    = darkMode ? "#e6edf3" : "#1a1a1a";
    const std::string qBg     = darkMode ? "#1c2a3a" : "#e3f2fd";
    const std::string aBg     = darkMode ? "#1c2a1c" : "#f1f8e9";
    const std::string mutedC  = darkMode ? "#8b949e" : "#666666";
    const std::string borderC = darkMode ? "#30363d" : "#d0d7de";

    std::string body;
    int idx = 0;
    for (const auto& t : turns) {
        std::string i = std::to_string(idx++);
        body += "<div class='turn'>"
                "<button class='del-btn' onclick='delTurn(" + i + ")' "
                "title='Delete this exchange'>\xc3\x97</button>"
                "<div class='q'>" + EscapeHTML(t.question) + "</div>"
                "<div class='a'>" + ProcessInline(t.answer) + "</div>"
                "</div>\n";
    }
    if (!pendingQ.empty()) {
        body += "<div class='turn'>"
                "<div class='q'>" + EscapeHTML(pendingQ) + "</div>"
                "<div class='a thinking'>&#x22EF;</div>"
                "</div>\n";
    }
    if (body.empty()) {
        body = "<p class='empty'>Ask anything about <em>"
               + EscapeHTML(chTitle) + "</em>.</p>";
    }

    return "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
           "<style>"
           "* { box-sizing: border-box; margin: 0; padding: 0 }"
           "body { font-family: -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
           "  font-size: 14px; line-height: 1.6; background: " + bg + "; color: " + text + ";"
           "  padding: 16px; }"
           ".turn { position: relative; margin-bottom: 20px; }"
           ".del-btn { float: right; background: transparent;"
           "  border: 1px solid transparent; cursor: pointer;"
           "  color: " + mutedC + "; font-size: 13px; padding: 1px 5px;"
           "  border-radius: 3px; line-height: 1; opacity: 0; transition: opacity .15s; }"
           ".turn:hover .del-btn { opacity: 1; }"
           ".del-btn:hover { border-color: " + borderC + "; }"
           ".q { background: " + qBg + "; border-radius: 8px 8px 8px 2px;"
           "  padding: 10px 14px; margin-bottom: 6px; font-weight: 500; }"
           ".a { background: " + aBg + "; border-radius: 2px 8px 8px 8px;"
           "  padding: 10px 14px; }"
           ".thinking { color: " + mutedC + "; font-style: italic; }"
           ".empty { color: " + mutedC + "; font-style: italic; padding: 8px 0; }"
           "code { background: rgba(128,128,128,.15); padding: .15em .35em; border-radius: 3px; }"
           "</style>"
           "<script>"
           "function delTurn(i){"
           "if(window.webkit&&window.webkit.messageHandlers&&"
           "window.webkit.messageHandlers.deleteTurn)"
           "window.webkit.messageHandlers.deleteTurn.postMessage(''+i);}"
           "</script>"
           "</head><body>" + body +
           "<script>window.scrollTo(0,document.body.scrollHeight);</script>"
           "</body></html>";
}

// ---------------------------------------------------------------------------
ChatFrame::ChatFrame(wxWindow*          parent,
                     const std::string& filePath,
                     int                chId,
                     const std::string& chTitle,
                     const LLMConfig&   llmCfg,
                     bool               darkMode,
                     std::function<void()> onSaved)
    : wxFrame(parent, wxID_ANY,
              wxString::FromUTF8("Chat \xe2\x80\x94 " + chTitle),
              wxDefaultPosition, wxSize(600, 560))
    , m_filePath(filePath)
    , m_chId(chId)
    , m_chTitle(chTitle)
    , m_llmCfg(llmCfg)
    , m_darkMode(darkMode)
    , m_onSaved(std::move(onSaved))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    m_webView = wxWebView::New(this, wxID_ANY, "about:blank");
    m_webView->AddScriptMessageHandler("deleteTurn");
    m_webView->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
                    &ChatFrame::OnScriptMessage, this);
    outer->Add(m_webView, 1, wxEXPAND | wxALL, 0);

    auto* inputRow = new wxBoxSizer(wxHORIZONTAL);
    m_inputCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                 wxDefaultPosition, wxSize(-1, 60),
                                 wxTE_MULTILINE | wxTE_PROCESS_ENTER);
    m_sendBtn = new wxButton(this, ID_CF_SEND, "Send");

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

    SetSizer(outer);

    m_inputCtrl->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
        wxCommandEvent dummy(wxEVT_BUTTON, ID_CF_SEND);
        OnSend(dummy);
    });

    LoadHistory();
    Render();
    Show();
}

// ---------------------------------------------------------------------------
void ChatFrame::LoadHistory() {
    m_turns = LoadConversation(m_filePath, m_chId);
}

// ---------------------------------------------------------------------------
void ChatFrame::Render(const std::string& pendingQ) {
    std::string html = ChatHTML(m_chTitle, m_turns, pendingQ, m_darkMode);
    m_webView->SetPage(wxString::FromUTF8(html), wxEmptyString);
}

// ---------------------------------------------------------------------------
void ChatFrame::OnScriptMessage(wxWebViewEvent& evt) {
    if (m_busy) return;
    long idx = -1;
    if (!evt.GetString().ToLong(&idx) || idx < 0 || idx >= (long)m_turns.size()) return;
    m_turns.erase(m_turns.begin() + (size_t)idx);
    DeleteTurn(m_filePath, m_chId, (int)idx);
    if (m_onSaved) m_onSaved();
    Render();
}

// ---------------------------------------------------------------------------
void ChatFrame::OnSend(wxCommandEvent&) {
    if (m_busy) return;
    wxString raw = m_inputCtrl->GetValue().Trim();
    if (raw.empty()) return;

    std::string question = raw.ToStdString();
    m_inputCtrl->Clear();
    m_busy = true;
    m_sendBtn->Enable(false);
    Render(question);   // show "thinking" bubble immediately

    std::string filePath = m_filePath;
    int         chId     = m_chId;
    std::string chTitle  = m_chTitle;
    LLMConfig   cfg      = m_llmCfg;

    // Read full document for context
    std::string docMarkdown;
    {
        std::ifstream f(filePath);
        if (f) docMarkdown.assign(std::istreambuf_iterator<char>(f), {});
    }

    std::vector<ConversationTurn> history = m_turns;
    std::string prompt = BuildQAPrompt(docMarkdown, chTitle, history, question);

    std::thread([this, prompt, cfg, filePath, chId, chTitle, question]() mutable {
        auto started = std::chrono::steady_clock::now();
        LLMResult res = InvokeLLM(prompt, cfg);
        int durationSeconds = (int)std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started).count();
        if (res.ok) {
            RecordLLMTiming(fs::path(filePath).parent_path().string(),
                            "chat", chTitle, durationSeconds);
        }

        wxTheApp->CallAfter([this, res, filePath, chId, chTitle, question]() {
            m_busy = false;
            m_sendBtn->Enable(true);

            std::string answer = res.ok ? res.text : ("Error: " + res.error);
            // Strip leading "A:" that some models echo back
            if (answer.rfind("A: ", 0) == 0) answer = answer.substr(3);
            if (answer.rfind("A:", 0) == 0)  answer = answer.substr(2);
            while (!answer.empty() && (answer.front() == ' ' || answer.front() == '\n'))
                answer.erase(answer.begin());

            ConversationTurn turn{question, answer};
            m_turns.push_back(turn);
            AppendTurn(filePath, chId, chTitle, turn);
            if (m_onSaved) m_onSaved();
            Render();
        });
    }).detach();
}
