#pragma once
#include <map>
#include <string>

// Image store: ~/test-taker/personas/
std::string GetPersonasDir();

// Lowercase, spaces→underscores, strip non-alphanumeric.
// Must match the JS norm() function in persona_tab_html.cpp.
std::string NormalizePersonaName(const std::string& name);

// Scan personas dir; return map of normalized_name → file:// URL (thumb).
// Returns empty map if the directory doesn't exist.
std::map<std::string, std::string> ScanPersonaImages();

// Overload that accepts an explicit directory path (useful for testing).
std::map<std::string, std::string> ScanPersonaImages(const std::string& dir);

// Copy srcImagePath into the personas dir under the given name.
// Creates the directory if needed. Returns destination path or "" on failure.
std::string AddPersonaImage(const std::string& personaName,
                             const std::string& srcImagePath);

// Convert a map of file:// URLs to data: URLs by reading and base64-encoding
// each file. Entries that can't be read are omitted.
std::map<std::string, std::string> ToDataURLs(
    const std::map<std::string, std::string>& fileUrls);

// Rename a persona's image file. Returns true if a file was found and renamed.
bool RenamePersonaImage(const std::string& oldName,
                        const std::string& newName,
                        const std::string& personasDir = "");
