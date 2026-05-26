#include "chat_html.h"
#include "conversation.h"
#include <iostream>
#include <string>

int test_chat_panel() {
    int failures = 0;

    // ── Input section ────────────────────────────────────────────────────────

    // Textarea and chatAction JS must be present when active.
    {
        ChatInputState s;
        s.hasProject = true;
        std::string html = BuildChatInputHTML(s);
        bool hasTa  = html.find("<textarea") != std::string::npos;
        bool hasJs  = html.find("chatAction") != std::string::npos;
        bool hasCtrl= html.find("cmd-enter") != std::string::npos
                   || html.find("Ctrl+Enter") != std::string::npos
                   || html.find("ctrlKey")    != std::string::npos
                   || html.find("metaKey")    != std::string::npos;
        if (!hasTa || !hasJs || !hasCtrl) {
            std::cerr << "FAIL [chat-input-has-textarea]: hasTa=" << hasTa
                      << " hasJs=" << hasJs << " hasCtrl=" << hasCtrl << "\n";
            ++failures;
        } else {
            std::cout << "PASS [chat-input-has-textarea]\n";
        }
    }

    // Textarea and send button must be disabled when busy.
    {
        ChatInputState s;
        s.hasProject = true;
        s.busy = true;
        std::string html = BuildChatInputHTML(s);
        bool taDisabled   = html.find("id='chat-ans'") != std::string::npos
                          ? html.find("id='chat-ans' disabled") != std::string::npos
                            || html.find("id=\"chat-ans\" disabled") != std::string::npos
                            || html.find("disabled") != std::string::npos
                          : false;
        // send button must be disabled
        bool sendDisabled = html.find("btn-chat-send") != std::string::npos
                         && html.find("disabled") != std::string::npos;
        if (!taDisabled || !sendDisabled) {
            std::cerr << "FAIL [chat-input-disabled-when-busy]: taDisabled="
                      << taDisabled << " sendDisabled=" << sendDisabled << "\n";
            ++failures;
        } else {
            std::cout << "PASS [chat-input-disabled-when-busy]\n";
        }
    }

    // Clear button must always be present.
    {
        ChatInputState s;
        s.hasProject = true;
        std::string html = BuildChatInputHTML(s);
        bool hasClear = html.find("clear") != std::string::npos
                     || html.find("Clear") != std::string::npos;
        if (!hasClear) {
            std::cerr << "FAIL [chat-input-has-clear]: no clear button\n";
            ++failures;
        } else {
            std::cout << "PASS [chat-input-has-clear]\n";
        }
    }

    // Flex-column body layout for sticky-bottom input (no position:fixed/sticky).
    {
        ChatInputState s;
        s.hasProject = true;
        std::string html = BuildChatInputHTML(s);
        bool hasFlex   = html.find("display:flex")         != std::string::npos;
        bool hasColumn = html.find("flex-direction:column") != std::string::npos;
        bool hasMinH   = html.find("min-height:100vh")      != std::string::npos;
        bool noFixed   = html.find("position:fixed")        == std::string::npos;
        bool noSticky  = html.find("position:sticky")       == std::string::npos;
        if (!hasFlex || !hasColumn || !hasMinH || !noFixed || !noSticky) {
            std::cerr << "FAIL [chat-input-flex-bottom]: flex=" << hasFlex
                      << " col=" << hasColumn << " minh=" << hasMinH
                      << " noFixed=" << noFixed << " noSticky=" << noSticky << "\n";
            ++failures;
        } else {
            std::cout << "PASS [chat-input-flex-bottom]\n";
        }
    }

    // When no project, textarea is disabled.
    {
        ChatInputState s;
        s.hasProject = false;
        std::string html = BuildChatInputHTML(s);
        bool disabled = html.find("disabled") != std::string::npos;
        if (!disabled) {
            std::cerr << "FAIL [chat-input-no-project-disables]: should be disabled without project\n";
            ++failures;
        } else {
            std::cout << "PASS [chat-input-no-project-disables]\n";
        }
    }

    // ── Chat turn HTML and floaters ──────────────────────────────────────────

    // Turn HTML must contain the correct testtaker://chat-explain/ URL.
    {
        ConversationTurn turn{"How does TCP work?", "TCP is a reliable protocol..."};
        std::string html = BuildChatTurnHTML(turn, 3, false, false, {});
        bool hasSave   = html.find("testtaker://chat-save/3")    != std::string::npos;
        bool hasLearn  = html.find("testtaker://chat-learnmore/3") != std::string::npos;
        if (!hasSave || !hasLearn) {
            std::cerr << "FAIL [chat-turn-urls]: hasSave=" << hasSave
                      << " hasLearn=" << hasLearn << "\n";
            ++failures;
        } else {
            std::cout << "PASS [chat-turn-urls]\n";
        }
    }

    // Personality explain URL uses testtaker://chat-explain/slug/idx format.
    {
        ConversationTurn turn{"What is RAII?", "RAII stands for..."};
        // Pass a dummy personality slug — any personality from kPersonalities works,
        // but we just need to confirm the URL pattern is present.
        std::string html = BuildChatTurnHTML(turn, 7, false, false, {"monkey"});
        bool hasExplain = html.find("testtaker://chat-explain/") != std::string::npos;
        bool hasIdx     = html.find("/7") != std::string::npos;
        if (!hasExplain || !hasIdx) {
            std::cerr << "FAIL [chat-turn-explain-url]: hasExplain=" << hasExplain
                      << " hasIdx=" << hasIdx << "\n";
            ++failures;
        } else {
            std::cout << "PASS [chat-turn-explain-url]\n";
        }
    }

    // Dropdown must use JS click-toggle, NOT CSS :hover, so clicks work in WKWebView.
    {
        ConversationTurn turn{"Q", "A"};
        std::string html = BuildChatTurnHTML(turn, 0, false, false, {"monkey"});
        // Must have a JS toggle function
        bool hasToggleJs = html.find("toggleDrop") != std::string::npos
                        || html.find("chatMenuToggle") != std::string::npos;
        // Must NOT rely solely on :hover to show the menu
        bool hoverOnly = html.find(":hover .chat-explain-menu") != std::string::npos
                      && html.find("toggleDrop") == std::string::npos
                      && html.find("chatMenuToggle") == std::string::npos;
        if (!hasToggleJs || hoverOnly) {
            std::cerr << "FAIL [chat-turn-dropdown-js-toggle]: hasToggleJs=" << hasToggleJs
                      << " hoverOnly=" << hoverOnly << "\n";
            ++failures;
        } else {
            std::cout << "PASS [chat-turn-dropdown-js-toggle]\n";
        }
    }

    // Saved turn shows saved label.
    {
        ConversationTurn turn{"Q", "A"};
        std::string html = BuildChatTurnHTML(turn, 0, false, true /*saved*/, {});
        bool hasSavedLabel = html.find("saved") != std::string::npos;
        if (!hasSavedLabel) {
            std::cerr << "FAIL [chat-turn-saved-label]\n";
            ++failures;
        } else {
            std::cout << "PASS [chat-turn-saved-label]\n";
        }
    }

    // BuildChatTurnHTML floats persona-img right inside .chat-a when thumbnail matches
    {
        ConversationTurn turn;
        turn.question = "\xf0\x9f\xa7\xa5 Columbo";
        turn.answer   = "Just one more thing...";
        std::map<std::string, std::string> thumbs = {
            {"\xf0\x9f\xa7\xa5 Columbo", "data:image/jpeg;base64,FAKEDATA"}
        };
        std::string html = BuildChatTurnHTML(turn, 0, false, false, {}, thumbs);
        bool hasImg  = html.find("persona-img") != std::string::npos;
        bool hasData = html.find("FAKEDATA")    != std::string::npos;
        if (!hasImg || !hasData) {
            std::cerr << "FAIL [chat-turn-persona-img]: hasImg=" << hasImg
                      << " hasData=" << hasData << "\n";
            ++failures;
        } else {
            std::cout << "PASS [chat-turn-persona-img]\n";
        }
    }

    // BuildChatTurnHTML shows persona-img for tidbit persona extracted from answer
    {
        ConversationTurn turn;
        turn.question = "What about quantum physics?";
        turn.answer   = ":::tidbit[Richard Feynman]\nSome insight.\n:::\n";
        std::map<std::string, std::string> thumbs = {
            {"richard_feynman", "data:image/jpeg;base64,FEYNMANDATA"}
        };
        std::string html = BuildChatTurnHTML(turn, 0, false, false, {}, thumbs);
        bool hasImg  = html.find("<img class='persona-img'") != std::string::npos;
        bool hasData = html.find("FEYNMANDATA") != std::string::npos;
        if (!hasImg || !hasData) {
            std::cerr << "FAIL [chat-turn-tidbit-img]: hasImg=" << hasImg
                      << " hasData=" << hasData << "\n";
            ++failures;
        } else {
            std::cout << "PASS [chat-turn-tidbit-img]\n";
        }
    }

    return failures;
}
