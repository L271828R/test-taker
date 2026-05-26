#include "chat_html.h"
#include "exam_prompt.h"   // RenderPersonalityDropdowns
#include "markdown.h"      // RenderMarkdown, EscapeHTML
#include <sstream>

// ---------------------------------------------------------------------------
std::string BuildChatInputHTML(const ChatInputState& s) {
    bool dis = s.busy || !s.hasProject;

    std::string placeholder = s.hasProject
        ? "Ask anything\xe2\x80\xa6"          // Ask anything…
        : "Activate a project to start chatting.";

    std::ostringstream out;
    out << R"(<style>
body{display:flex;flex-direction:column;min-height:100vh;}
#chat-input-bar{background:var(--bg);border-top:1px solid var(--border);
  padding:8px 12px 4px;}
#chat-input-row{display:flex;gap:8px;align-items:flex-end;}
#chat-ans{flex:1;min-height:80px;resize:vertical;padding:8px;
  border:1px solid var(--border);border-radius:4px;
  background:var(--surface);color:var(--text);
  font-family:inherit;font-size:1em;line-height:1.4;}
#chat-ans:focus{outline:2px solid var(--link);outline-offset:-1px;}
.chat-ibtn{padding:6px 12px;border-radius:4px;border:1px solid var(--border);
  background:var(--surface);color:var(--text);cursor:pointer;
  font-size:.85em;white-space:nowrap;}
.chat-ibtn:hover:not(:disabled){background:var(--link);color:#fff;border-color:var(--link);}
.chat-ibtn:disabled{opacity:.38;cursor:default;}
#btn-chat-send{background:var(--link);color:#fff;border-color:var(--link);font-weight:600;}
#btn-chat-send:disabled{background:var(--surface);color:var(--text-muted);border-color:var(--border);}
</style>
)";

    auto d = [&](bool disabled) -> std::string { return disabled ? " disabled" : ""; };

    out << "<div id='chat-input-bar'>\n"
        << "<div id='chat-input-row'>\n"
        << "<textarea id='chat-ans' placeholder='" << EscapeHTML(placeholder) << "'"
        << d(dis) << "></textarea>\n"
        << "<div style='display:flex;flex-direction:column;gap:4px'>\n"
        << "<button id='btn-chat-send' class='chat-ibtn' onclick='chatAction(\"send\")'"
        << d(dis) << ">Send</button>\n"
        << "<button id='btn-chat-clear' class='chat-ibtn' onclick='chatAction(\"clear\")'"
        << d(s.busy) << ">Clear</button>\n"
        << "</div>\n"
        << "</div>\n"
        << "</div>\n";

    out << R"(<script>
function chatAction(act){
  var t=document.getElementById('chat-ans');
  var payload=JSON.stringify({action:act,text:(t?t.value:'')});
  if(window.webkit&&window.webkit.messageHandlers&&window.webkit.messageHandlers.chatAction)
    window.webkit.messageHandlers.chatAction.postMessage(payload);
}
(function(){
  var a=document.getElementById('chat-ans');
  if(!a)return;
  a.addEventListener('keydown',function(e){
    if((e.metaKey||e.ctrlKey)&&e.key==='Enter'){chatAction('send');}
  });
})();
</script>
)";

    return out.str();
}

// ---------------------------------------------------------------------------
// Per-fragment JS snippet included for testability; page-level version in BuildChatHTML.
static std::string ChatDropToggleJS() {
    return PersonalityDropdownJS("chat-explain-drop", "chat-explain-btn");
}

std::string BuildChatTurnHTML(const ConversationTurn& turn,
                               int idx,
                               bool darkMode,
                               bool saved,
                               const std::vector<std::string>& personalities,
                               const std::map<std::string, std::string>& thumbnails) {
    (void)darkMode;    // colours handled by CSS variables
    (void)personalities; // tidbit chars — unrelated to explain-like buttons

    std::string si        = std::to_string(idx);
    std::string saveClass = saved ? " saved" : "";
    std::string saveLabel = saved ? "&#x1F516; saved" : "&#x1F516; save";

    // Always show personality dropdowns — they come from kPersonalities which
    // is always populated, independent of per-project tidbit characters.
    std::string explainHtml = RenderPersonalityDropdowns(
        "testtaker://chat-explain/", "/" + si,
        "chat-explain-drop", "chat-explain-btn", "chat-explain-menu");

    auto thumbIt = thumbnails.find(turn.question);
    std::string avatarHtml;
    if (thumbIt != thumbnails.end())
        avatarHtml = "<img class='persona-img' src='" + thumbIt->second + "' alt=''>";

    std::string html =
        "<div class='chat-turn'>"
        "<div class='chat-toolbar'>"
        "<a class='chat-save-btn" + saveClass + "' href='testtaker://chat-save/" + si + "'>"
        + saveLabel + "</a>"
        + explainHtml
        + "<a class='chat-floater-btn' href='testtaker://chat-learnmore/" + si + "' "
          "title='Deep dive: learn more'>&#x1F4D6; learn&nbsp;more</a>"
          "</div>"
          "<div class='chat-q'>" + EscapeHTML(turn.question) + "</div>"
          "<div class='chat-a'>" + avatarHtml + RenderMarkdown(turn.answer) + "</div>"
          "</div>\n";

    // Include toggle JS in every fragment so unit tests can verify it.
    // chat_panel.cpp deduplicates by placing it once in the page <head> via BuildHTML.
    html = ChatDropToggleJS() + html;

    return html;
}
