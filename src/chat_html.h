#pragma once
#include <string>
#include <vector>
#include "conversation.h"

// State snapshot for the sticky input footer in the Chat tab.
struct ChatInputState {
    bool busy       = false;   // disable interactive elements
    bool hasProject = false;   // false → disable input (no project loaded)
};

// Build the sticky HTML input footer for the Chat tab.
// Always returns non-empty HTML (input is disabled when hasProject=false).
std::string BuildChatInputHTML(const ChatInputState& s);

// Build the HTML fragment for a single conversation turn, including the
// toolbar (save, personality dropdowns, learn-more).
// The dropdown uses a JS click-toggle so clicks work reliably in WKWebView.
std::string BuildChatTurnHTML(const ConversationTurn& turn,
                               int idx,
                               bool darkMode,
                               bool saved,
                               const std::vector<std::string>& personalities);
