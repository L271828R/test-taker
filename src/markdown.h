#pragma once
#include <map>
#include <string>

// Pure functions — no wxWidgets dependency, directly unit-testable.

std::string EscapeHTML(const std::string& text);
std::string ProcessInline(const std::string& text);
std::string RenderMarkdown(const std::string& md);

// Replace <!-- note:N --> markers with rendered note spans.
// noteTexts maps note id → note body text.
std::string InjectNoteSpans(const std::string& mdContent,
                            const std::map<int,std::string>& noteTexts);

// Returns a markdown document describing every syntax feature MDViewer
// supports. Printed to stdout when the binary is invoked with --llm.
std::string GetLLMReadme();
