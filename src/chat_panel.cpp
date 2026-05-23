#include "chat_panel.h"
#include "chat_html.h"
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
#include <wx/app.h>
#include <wx/sizer.h>

namespace fs = std::filesystem;

wxBEGIN_EVENT_TABLE(ChatPanel, wxPanel)
    EVT_WEBVIEW_NAVIGATING(wxID_ANY, ChatPanel::OnWebViewNav)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
ChatPanel::ChatPanel(wxWindow* parent, bool darkMode, SavedConvoCallback onSavedConvo)
    : wxPanel(parent), m_darkMode(darkMode), m_onSavedConvo(std::move(onSavedConvo))
{
    auto* outer = new wxBoxSizer(wxVERTICAL);

    m_webView = wxWebView::New(this, wxID_ANY, "about:blank");
    m_webView->SetPage(
        "<html><head><script>function chatAction(a,t){}</script></head><body></body></html>", "");
    wxTheApp->CallAfter([this]() {
        if (m_webView) m_webView->AddScriptMessageHandler("chatAction");
    });
    m_webView->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
                    &ChatPanel::OnChatAction, this);

    outer->Add(m_webView, 1, wxEXPAND);
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
        body += BuildChatTurnHTML(m_turns[i], i, m_darkMode,
                                  m_savedIndices.count(i) > 0,
                                  m_personalities);
    }
    if (!pendingQ.empty()) {
        body += "<div class='chat-turn'>"
                "<div class='chat-q'>" + EscapeHTML(pendingQ) + "</div>"
                "<div class='chat-a thinking'>&#x22EF;</div>"
                "</div>\n";
    }
    if (m_turns.empty() && pendingQ.empty()) {
        body += m_chatFile.empty()
            ? "<p class='empty'>Activate a project to start chatting.</p>"
            : "<p class='empty'>Ask anything about your study topic.</p>";
    }

    body += "<div id='chat-bottom'></div>"
            "<script>requestAnimationFrame(function(){"
            "var b=document.getElementById('chat-bottom');"
            "if(b)b.scrollIntoView({behavior:'instant'});});</script>";

    const std::string dropCSS =
        PersonalityDropdownCSS("chat-explain-drop", "chat-explain-btn",
                               "chat-explain-menu", ".chat-turn");
    const std::string extraCSS = "<style>\n"
        ".chat-turn { margin-bottom:18px; }\n"
        ".chat-toolbar { display:flex; gap:0.4em; margin-bottom:0.3em; flex-wrap:wrap; }\n"
        ".chat-turn:hover .chat-save-btn,"
        ".chat-turn:hover .chat-floater-btn { opacity:1; }\n"
        ".chat-save-btn, .chat-floater-btn {\n"
        "  opacity:0; transition:opacity 0.15s;\n"
        "  background:none; border:1px solid var(--border); border-radius:4px;\n"
        "  padding:0.15em 0.5em; font-size:0.82em; cursor:pointer;\n"
        "  color:var(--text-muted); text-decoration:none; white-space:nowrap; }\n"
        ".chat-save-btn.saved { color:#1a7f37; border-color:#1a7f37; opacity:1; }\n"
        ".chat-floater-btn:hover { border-color:#9a6700; color:#9a6700; }\n"
        + dropCSS
        + ".chat-q { background:" + qBg + "; border-radius:8px 8px 8px 2px;\n"
          "  padding:10px 14px; margin-bottom:6px; font-weight:500; }\n"
          ".chat-a { background:" + aBg + "; border-radius:2px 8px 8px 8px;\n"
          "  padding:10px 14px; }\n"
          ".chat-a pre { margin:8px 0; }\n"
          ".thinking { color:var(--text-muted); font-style:italic; }\n"
          ".empty    { color:var(--text-muted); font-style:italic; }\n"
          "</style>";

    const std::string dropToggleJS =
        PersonalityDropdownJS("chat-explain-drop", "chat-explain-btn");

    ChatInputState inputState;
    inputState.busy       = m_busy;
    inputState.hasProject = !m_chatFile.empty();
    std::string inputSection = BuildChatInputHTML(inputState);

    std::string content = "<div style='flex:1'>" + body + "</div>";
    return BuildHTML(extraCSS + dropToggleJS + content + inputSection, "Chat", m_darkMode);
}

// ---------------------------------------------------------------------------
static std::string chatJsonField(const std::string& json, const std::string& key) {
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

void ChatPanel::OnChatAction(wxWebViewEvent& evt) {
    std::string payload = evt.GetString().ToStdString();
    std::string action  = chatJsonField(payload, "action");
    std::string text    = chatJsonField(payload, "text");

    if (action == "send") {
        if (text.empty()) return;
        SendMessage(text);
    } else if (action == "clear") {
        if (m_chatFile.empty()) return;
        int ans = wxMessageBox("Clear all chat history for this project?",
                               "Clear Chat", wxYES_NO | wxICON_WARNING);
        if (ans != wxYES) return;
        std::ofstream f(m_chatFile, std::ios::trunc);
        f << "# Chat\n\n<!-- ch:0 -->\n## Conversation\n";
        m_turns.clear();
        m_savedIndices.clear();
        Logger::get().log("Chat history cleared");
        Render();
    }
}

void ChatPanel::SendMessage(const std::string& question) {
    if (m_busy || m_chatFile.empty()) return;
    if (question.empty()) return;

    m_busy = true;
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
void ChatPanel::FireAsNewTurn(const std::string& displayQuestion,
                               const std::string& prompt) {
    if (m_busy || m_chatFile.empty()) return;
    m_busy = true;
    Render(displayQuestion);

    LLMConfig   cfg      = m_llmCfg;
    std::string chatFile = m_chatFile;

    std::thread([this, prompt, cfg, chatFile, displayQuestion]() {
        auto res = InvokeLLM(prompt, cfg);
        wxTheApp->CallAfter([this, res, chatFile, displayQuestion]() {
            m_busy = false;

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
