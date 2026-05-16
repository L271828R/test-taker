#include "chat_panel.h"
#include "conversation.h"
#include "markdown.h"
#include "meta.h"
#include "logger.h"
#include <fstream>
#include <thread>
#include <filesystem>
#include <wx/sizer.h>

namespace fs = std::filesystem;

enum { ID_CP_SEND = wxID_HIGHEST + 600 };

wxBEGIN_EVENT_TABLE(ChatPanel, wxPanel)
    EVT_BUTTON(ID_CP_SEND, ChatPanel::OnSend)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
ChatPanel::ChatPanel(wxWindow* parent, bool darkMode)
    : wxPanel(parent), m_darkMode(darkMode)
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    m_webView = wxWebView::New(this, wxID_ANY, "about:blank");
    outer->Add(m_webView, 1, wxEXPAND);

    auto* inputRow = new wxBoxSizer(wxHORIZONTAL);
    m_inputCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxSize(-1, 70),
        wxTE_MULTILINE | wxTE_PROCESS_ENTER);
    m_sendBtn = new wxButton(this, ID_CP_SEND, "Send");

    m_inputCtrl->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
        wxCommandEvent dummy(wxEVT_BUTTON, ID_CP_SEND);
        OnSend(dummy);
    });

    inputRow->Add(m_inputCtrl, 1, wxEXPAND | wxALL, 6);
    inputRow->Add(m_sendBtn, 0, wxALIGN_BOTTOM | wxALL, 6);
    outer->Add(inputRow, 0, wxEXPAND);
    SetSizer(outer);

    Render();
}

// ---------------------------------------------------------------------------
void ChatPanel::SyncProject(const std::string& projectDir,
                             const LLMConfig&   llmCfg,
                             bool               darkMode) {
    m_llmCfg   = llmCfg;
    m_darkMode = darkMode;
    m_turns.clear();

    if (projectDir.empty()) {
        m_chatFile.clear();
        Render();
        return;
    }

    m_chatFile = projectDir + "/chat.md";

    // Create chat.md if it doesn't exist yet.
    if (!fs::exists(m_chatFile)) {
        std::ofstream f(m_chatFile);
        f << "# Chat\n\n<!-- ch:0 -->\n## Conversation\n";
    }

    LoadHistory();
    Render();
}

// ---------------------------------------------------------------------------
void ChatPanel::LoadHistory() {
    if (m_chatFile.empty()) return;
    m_turns = LoadConversation(m_chatFile, 0);
}

// ---------------------------------------------------------------------------
void ChatPanel::Render(const std::string& pendingQ) {
    std::string html = BuildChatHTML(pendingQ);
    m_webView->SetPage(wxString::FromUTF8(html), wxEmptyString);
}

// ---------------------------------------------------------------------------
std::string ChatPanel::BuildChatHTML(const std::string& pendingQ) const {
    const std::string bg      = m_darkMode ? "#0d1117" : "#ffffff";
    const std::string text    = m_darkMode ? "#e6edf3" : "#1a1a1a";
    const std::string qBg     = m_darkMode ? "#1c2a3a" : "#e3f2fd";
    const std::string aBg     = m_darkMode ? "#1c2a1c" : "#f1f8e9";
    const std::string mutedC  = m_darkMode ? "#8b949e" : "#666666";

    std::string body;
    for (const auto& t : m_turns) {
        body += "<div class='turn'>"
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
        if (m_chatFile.empty())
            body = "<p class='empty'>Activate a project to start chatting.</p>";
        else
            body = "<p class='empty'>Ask anything about your study topic.</p>";
    }

    return "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
           "<style>"
           "* { box-sizing:border-box; margin:0; padding:0 }"
           "body { font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
           "  font-size:14px; line-height:1.6;"
           "  background:" + bg + "; color:" + text + "; padding:16px; }"
           ".turn { margin-bottom:18px; }"
           ".q { background:" + qBg + "; border-radius:8px 8px 8px 2px;"
           "  padding:10px 14px; margin-bottom:6px; font-weight:500; }"
           ".a { background:" + aBg + "; border-radius:2px 8px 8px 8px;"
           "  padding:10px 14px; }"
           ".thinking { color:" + mutedC + "; font-style:italic; }"
           ".empty { color:" + mutedC + "; font-style:italic; padding:8px 0; }"
           "code { background:rgba(128,128,128,.15); padding:.15em .35em; border-radius:3px; }"
           "</style>"
           "</head><body>" + body +
           "<script>window.scrollTo(0,document.body.scrollHeight);</script>"
           "</body></html>";
}

// ---------------------------------------------------------------------------
void ChatPanel::OnSend(wxCommandEvent&) {
    if (m_busy || m_chatFile.empty()) return;
    wxString raw = m_inputCtrl->GetValue().Trim();
    if (raw.empty()) return;

    std::string question = raw.ToStdString();
    m_inputCtrl->Clear();
    m_busy = true;
    m_sendBtn->Enable(false);
    Render(question);

    // Load context.md if present — injected into prompt as background material.
    std::string contextMd;
    {
        std::string ctxPath = fs::path(m_chatFile).parent_path().string() + "/context.md";
        std::ifstream f(ctxPath);
        if (f) contextMd.assign(std::istreambuf_iterator<char>(f), {});
    }

    std::vector<ConversationTurn> history = m_turns;
    std::string prompt = BuildQAPrompt(contextMd, "Study Chat", history, question);
    LLMConfig   cfg    = m_llmCfg;
    std::string chatFile = m_chatFile;

    std::thread([this, prompt, cfg, chatFile, question]() {
        auto res = InvokeLLM(prompt, cfg);
        wxTheApp->CallAfter([this, res, chatFile, question]() {
            m_busy = false;
            m_sendBtn->Enable(true);

            std::string answer = res.ok ? res.text : ("Error: " + res.error);
            while (!answer.empty() && (answer.front() == ' ' || answer.front() == '\n'))
                answer.erase(answer.begin());

            ConversationTurn turn{question, answer};
            m_turns.push_back(turn);
            AppendTurn(chatFile, 0, "Conversation", turn);
            Render();
        });
    }).detach();
}
