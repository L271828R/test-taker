#pragma once
#include <string>

// Wraps a rendered body in a full HTML page with CSS, Mermaid.js,
// highlight.js, and runtime font-size / dark-mode controls.
// Pure function — no wxWidgets dependency, directly unit-testable.
std::string BuildHTML(const std::string& body,
                      const std::string& title,
                      bool darkMode,
                      int fontSizePercent = 100);

// Renders the application log file as a themed HTML page.
// Pure function — no wxWidgets dependency, directly unit-testable.
std::string BuildLogsHTML(const std::string& rawLog,
                           const std::string& logPath,
                           bool darkMode);

// Renders the RAG log file as a themed HTML page showing retrieval events.
// Each event shows the query, retrieved chunks with scores and doc names, and full text.
// Pure function — no wxWidgets dependency, directly unit-testable.
std::string BuildRagLogsHTML(const std::string& rawLog,
                              const std::string& logPath,
                              bool darkMode);
