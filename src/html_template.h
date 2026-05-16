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
