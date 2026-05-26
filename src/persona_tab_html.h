#pragma once
#include <string>

// Build the full HTML page for the Personas management tab.
// Pure function — no wxWidgets dependency, directly unit-testable.
std::string BuildPersonaTabHTML(bool darkMode);
