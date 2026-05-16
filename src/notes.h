#pragma once
#include <string>
#include <vector>

struct Note {
    int         id;
    std::string anchor;        // "note:7"
    std::string selectedText;  // the text the user highlighted
    std::string text;          // the note body
    std::string file;          // basename of the .md file, e.g. "ch01_....md"
};

// Load/save notes.json in projectDir.
std::vector<Note> LoadNotes(const std::string& projectDir);
bool              SaveNotes(const std::string& projectDir, const std::vector<Note>& notes);

// Return next available note ID (max existing + 1, or 1 if empty).
int NextNoteId(const std::vector<Note>& notes);

// Insert <!-- note:N --> immediately after the first occurrence of selectedText
// within context (a ~100-char surrounding snippet sent from JS) inside mdContent.
// Returns the modified string, or mdContent unchanged if the anchor point is not found.
std::string InsertNoteAnchor(const std::string& mdContent,
                             const std::string& selectedText,
                             const std::string& context,
                             int noteId);

// Remove <!-- note:N --> from mdContent. Returns modified string.
std::string RemoveNoteAnchor(const std::string& mdContent, int noteId);

// Scan mdContent for <!-- note:N --> markers. For each found, look it up in notes,
// collect its text, strip ALL note markers from mdContent, then append a
// "## Notes (from previous version)" section listing each rescued note.
// Returns the transformed content. Updates notes in-place (removes rescued entries).
std::string RescueOrphanedNotes(const std::string& mdContent,
                                std::vector<Note>& notes,
                                const std::string& mdBasename);
