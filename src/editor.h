#pragma once
#include <string>
#include <vector>

// Returns the :::tidbit...:::  block that follows the <!-- tb:N --> marker
// in fileContent. Returns "" if the marker is not found.
std::string ExtractTidbit(const std::string& fileContent, int tidbitId);

// Returns a copy of fileContent with the tidbit block following <!-- tb:N -->
// replaced by newBlock. The <!-- tb:N --> marker itself is preserved.
// Returns fileContent unchanged if the marker is not found.
std::string PatchTidbit(const std::string& fileContent,
                        int tidbitId,
                        const std::string& newBlock);

// Reads the file at filepath, applies PatchTidbit, and writes the result back.
// Returns true on success.
bool ApplyTidbitPatch(const std::string& filepath,
                      int tidbitId,
                      const std::string& newBlock);

// Overwrites the file at filepath with newContent.
// Returns true on success.
bool ReplaceChapter(const std::string& filepath, const std::string& newContent);

// Returns the block from <!-- ch:N --> up to (not including) the next <!-- ch: -->
// marker, or to end-of-file. Returns "" if the marker is not found.
std::string ExtractChapter(const std::string& fileContent, int chapterId);

// Reads the file at filepath, replaces the section for chapterId with newBlock,
// and writes the result back. Returns true on success.
bool ApplyChapterPatch(const std::string& filepath,
                       int chapterId,
                       const std::string& newBlock);

// Sorts project files using a persisted order list first, then appends new files
// alphabetically. Missing files are ignored.
std::vector<std::string> ApplyFileOrder(const std::vector<std::string>& files,
                                        const std::vector<std::string>& savedOrder);

// Returns the file index to select after refreshing the file list.
// Keeps the previous filename selected when it is still present.
int RefreshedFileSelectionIndex(const std::vector<std::string>& files,
                                const std::string& previousFile);

// Reads/writes <projectDir>/.file_order, one filename per line.
std::vector<std::string> LoadFileOrder(const std::string& projectDir);
bool SaveFileOrder(const std::string& projectDir,
                   const std::vector<std::string>& files);
