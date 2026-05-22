#include "chat_panel.h"
#include "conversation.h"
#include "corpus.h"
#include "exam_prompt.h"
#include "html_template.h"
#include "markdown.h"
#include "meta.h"
#include "logger.h"
#include "project.h"
#include "saved_convos.h"
#include <ctime>
#include <fstream>
#include <thread>
#include <filesystem>
#include <wx/sizer.h>

namespace fs = std::filesystem;

enum { ID_CP_SEND = wxID_HIGHEST + 600, ID_CP_CLEAR };

wxBEGIN_EVENT_TABLE(ChatPanel, wxPanel)
    EVT_BUTTON(ID_CP_SEND,  ChatPanel::OnSend)
    EVT_BUTTON(ID_CP_CLEAR, ChatPanel::OnClearChat)
    EVT_WEBVIEW_NAVIGATING(wxID_ANY, ChatPanel::OnWebViewNav)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
ChatPanel::ChatPanel(wxWindow* parent, bool darkMode, SavedConvoCallback onSavedConvo)
    : wxPanel(parent), m_darkMode(darkMode), m_onSavedConvo(std::move(onSavedConvo))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    m_webView = wxWebView::New(this, wxID_ANY, "about:blank");
    outer->Add(m_webView, 1, wxEXPAND);

    auto* inputRow = new wxBoxSizer(wxHORIZONTAL);
    m_inputCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxSize(-1, 70),
        wxTE_MULTILINE);
    m_sendBtn  = new wxButton(this, ID_CP_SEND,  "Send (Ctrl+Enter)");
    m_clearBtn = new wxButton(this, ID_CP_CLEAR, "Clear Chat");

    m_inputCtrl->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_RETURN && e.ControlDown()) {
            wxCommandEvent dummy(wxEVT_BUTTON, ID_CP_SEND);
            OnSend(dummy);
        } else {
            e.Skip();
        }
    });

    auto* btnCol = new wxBoxSizer(wxVERTICAL);
    btnCol->Add(m_sendBtn,  0, wxEXPAND | wxBOTTOM, 4);
    btnCol->Add(m_clearBtn, 0, wxEXPAND);

    inputRow->Add(m_inputCtrl, 1, wxEXPAND | wxALL, 6);
    inputRow->Add(btnCol, 0, wxALIGN_BOTTOM | wxALL, 6);
    outer->Add(inputRow, 0, wxEXPAND);
    SetSizer(outer);

    Render();
}

// ---------------------------------------------------------------------------
void ChatPanel::SyncProject(const std::string& projectDir,
                             const LLMConfig&   llmCfg,
                             bool               darkMode) {
    m_llmCfg       = llmCfg;
    m_darkMode     = darkMode;
    m_turns.clear();
    m_savedIndices.clear();

    if (projectDir.empty()) {
        m_chatFile.clear();
        Render();
        return;
    }

    m_projectDir = projectDir;
    m_chatFile   = projectDir + "/chat.md";

    // Load personalities for tidbit injection.
    {
        ProjectConfig pcfg = LoadConfig(projectDir);
        m_personalities.clear();
        std::string p = pcfg.personalities;
        size_t start = 0, end;
        while ((end = p.find('|', start)) != std::string::npos) {
            std::string name = p.substr(start, end - start);
            if (!name.empty()) m_personalities.push_back(name);
            start = end + 1;
        }
        if (start < p.size()) m_personalities.push_back(p.substr(start));
    }

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
    const std::string qBg = m_darkMode ? "#1c2a3a" : "#e3f2fd";
    const std::string aBg = m_darkMode ? "#1c2a1c" : "#f1f8e9";

    std::string body;
    for (int i = 0; i < (int)m_turns.size(); ++i) {
        const auto& t = m_turns[i];
        bool isSaved = m_savedIndices.count(i) > 0;
        std::string saveClass = isSaved ? " saved" : "";
        std::string saveLabel = isSaved ? "&#x1F516; saved" : "&#x1F516; save";
        std::string si = std::to_string(i);
        body += "<div class='chat-turn'>"
                "<div class='chat-toolbar'>"
                "<a class='chat-save-btn" + saveClass + "' href='testtaker://chat-save/" + si + "'>" + saveLabel + "</a>"
                + RenderPersonalityDropdowns(
                    "testtaker://chat-explain/", "/" + si,
                    "chat-explain-drop", "chat-explain-btn", "chat-explain-menu")
                + "<a class='chat-floater-btn' href='testtaker://chat-learnmore/" + si + "' title='Deep dive: learn more'>&#x1F4D6; learn&nbsp;more</a>"
                "</div>"
                "<div class='chat-q'>" + EscapeHTML(t.question) + "</div>"
                "<div class='chat-a'>" + RenderMarkdown(t.answer) + "</div>"
                "</div>\n";
    }
    if (!pendingQ.empty()) {
        body += "<div class='chat-turn'>"
                "<div class='chat-q'>" + EscapeHTML(pendingQ) + "</div>"
                "<div class='chat-a thinking'>&#x22EF;</div>"
                "</div>\n";
    }
    if (body.empty()) {
        if (m_chatFile.empty())
            body = "<p class='empty'>Activate a project to start chatting.</p>";
        else
            body = "<p class='empty'>Ask anything about your study topic.</p>";
    }

    body += "<script>window.scrollTo(0,document.body.scrollHeight);</script>";

    const std::string extraCSS = R"(<style>
.chat-turn { margin-bottom:18px; }
.chat-toolbar { display:flex; gap:0.4em; margin-bottom:0.3em; }
.chat-turn:hover .chat-save-btn,
.chat-turn:hover .chat-floater-btn { opacity:1; }
.chat-save-btn, .chat-floater-btn {
  opacity:0; transition:opacity 0.15s;
  background:none; border:1px solid var(--border); border-radius:4px;
  padding:0.15em 0.5em; font-size:0.82em; cursor:pointer;
  color:var(--text-muted); text-decoration:none; white-space:nowrap; }
.chat-save-btn.saved { color:#1a7f37; border-color:#1a7f37; opacity:1; }
.chat-floater-btn:hover { border-color:#9a6700; color:#9a6700; }
.chat-explain-drop { position:relative; display:inline-block; opacity:0; transition:opacity 0.15s; }
.chat-turn:hover .chat-explain-drop { opacity:1; }
.chat-explain-btn { background:none; border:1px solid var(--border); border-radius:4px;
  padding:0.15em 0.5em; font-size:0.82em; cursor:pointer;
  color:var(--text-muted); white-space:nowrap; }
.chat-explain-drop:hover .chat-explain-menu,
.chat-explain-drop:focus-within .chat-explain-menu { display:block; }
.chat-explain-menu { display:none; position:absolute; top:100%; left:0;
  min-width:160px; background:var(--surface);
  border:1px solid var(--border); border-radius:4px;
  box-shadow:0 4px 12px rgba(0,0,0,.18); z-index:999; padding:2px 0; }
.chat-explain-menu a { display:block; padding:5px 11px; color:var(--text);
  text-decoration:none; font-size:.82em; white-space:nowrap; }
.chat-explain-menu a:hover { background:var(--surface-hover,rgba(0,0,0,.06)); }
.chat-q { background:)" + qBg + R"(; border-radius:8px 8px 8px 2px;
  padding:10px 14px; margin-bottom:6px; font-weight:500; }
.chat-a { background:)" + aBg + R"(; border-radius:2px 8px 8px 8px;
  padding:10px 14px; }
.chat-a pre { margin:8px 0; }
.thinking { color:var(--text-muted); font-style:italic; }
.empty    { color:var(--text-muted); font-style:italic; }
</style>)";

    return BuildHTML(extraCSS + body, "Chat", m_darkMode);
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

    std::string projectDir = fs::path(m_chatFile).parent_path().string();

    // Load context.md if present — injected into prompt as background material.
    std::string contextMd;
    {
        std::ifstream f(projectDir + "/context.md");
        if (f) contextMd.assign(std::istreambuf_iterator<char>(f), {});
    }

    // Prepend relevant corpus excerpts when available.
    std::string corpusCtx = CorpusContextFor(projectDir, question, m_llmCfg.ollamaUrl, "Chat",
                                              CorpusTopK(m_llmCfg.backend));
    if (!corpusCtx.empty()) {
        contextMd = corpusCtx + (contextMd.empty() ? "" : "\n\n" + contextMd);
    }

    std::vector<ConversationTurn> history = m_turns;
    std::string prompt = BuildQAPrompt(contextMd, "Study Chat", history, question,
                                       m_personalities);
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
            bool saved = AppendTurn(chatFile, 0, "Conversation", turn);
            Logger::get().log("Chat turn saved=" + std::to_string(saved)
                              + "  q=" + question.substr(0, 60));
            Render();
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void ChatPanel::OnClearChat(wxCommandEvent&) {
    if (m_chatFile.empty()) return;
    int answer = wxMessageBox("Clear all chat history for this project?",
                              "Clear Chat", wxYES_NO | wxICON_WARNING);
    if (answer != wxYES) return;

    // Truncate chat.md to just the header
    std::ofstream f(m_chatFile, std::ios::trunc);
    f << "# Chat\n\n<!-- ch:0 -->\n## Conversation\n";
    m_turns.clear();
    m_savedIndices.clear();
    Logger::get().log("Chat history cleared");
    Render();
}

// ---------------------------------------------------------------------------
void ChatPanel::FireAsNewTurn(const std::string& displayQuestion,
                               const std::string& prompt) {
    if (m_busy || m_chatFile.empty()) return;
    m_busy = true;
    m_sendBtn->Enable(false);
    Render(displayQuestion);

    LLMConfig   cfg      = m_llmCfg;
    std::string chatFile = m_chatFile;

    std::thread([this, prompt, cfg, chatFile, displayQuestion]() {
        auto res = InvokeLLM(prompt, cfg);
        wxTheApp->CallAfter([this, res, chatFile, displayQuestion]() {
            m_busy = false;
            m_sendBtn->Enable(true);

            std::string answer = res.ok ? res.text : ("Error: " + res.error);
            while (!answer.empty() && (answer.front() == ' ' || answer.front() == '\n'))
                answer.erase(answer.begin());

            ConversationTurn turn{displayQuestion, answer};
            m_turns.push_back(turn);
            AppendTurn(chatFile, 0, "Conversation", turn);
            Render();
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void ChatPanel::OnWebViewNav(wxWebViewEvent& evt) {
    wxString url = evt.GetURL();

    if (url.StartsWith("testtaker://chat-save/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(22).ToLong(&idx);  // "testtaker://chat-save/" is 22 chars
        if (idx < 0 || idx >= (long)m_turns.size()) return;
        if (m_savedIndices.count((int)idx)) return;  // already saved

        std::time_t now = std::time(nullptr);
        char dateBuf[16];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", std::localtime(&now));

        const auto& t = m_turns[idx];
        AppendSavedConvo(m_projectDir, t.question, t.answer, dateBuf);
        m_savedIndices.insert((int)idx);
        Logger::get().log("Chat turn " + std::to_string(idx) + " saved to saved_convos.md");

        if (m_onSavedConvo) m_onSavedConvo();
        Render();
        return;
    }

    if (url.StartsWith("testtaker://chat-explain/")) {
        evt.Veto();
        // URL: testtaker://chat-explain/{slug}/{idx}
        wxString rest = url.Mid(25);  // after "testtaker://chat-explain/"
        int slash = rest.Find('/');
        if (slash < 0) return;
        std::string slug = rest.Left(slash).ToStdString();
        long idx = -1;
        rest.Mid(slash + 1).ToLong(&idx);
        if (idx < 0 || idx >= (long)m_turns.size()) return;
        const PersonalityDef* def = FindPersonality(slug);
        if (!def) return;
        const auto& t = m_turns[idx];
        std::string prompt = BuildPersonalityPrompt(*def, t.question, "", t.answer);
        FireAsNewTurn(def->displayQ, prompt);
        return;
    }

    if (url.StartsWith("testtaker://chat-learnmore/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(27).ToLong(&idx);  // "testtaker://chat-learnmore/" is 27 chars
        if (idx < 0 || idx >= (long)m_turns.size()) return;
        const auto& t = m_turns[idx];
        std::string prompt = BuildLearnMorePrompt(t.question, t.answer);
        FireAsNewTurn("\xf0\x9f\x93\x96 Deep dive: learn more", prompt);
        return;
    }

    evt.Skip();
}
