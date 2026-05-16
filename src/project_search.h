#pragma once
#include <string>

// Case-insensitive text search used by the Projects tab.
// Every whitespace-separated query term must appear in the searchable text.
bool ProjectSearchTextMatches(const std::string& searchableText,
                              const std::string& query);

// Build a searchable index for a project from its visible metadata plus the
// filenames and contents of .md files in the project directory.
std::string BuildProjectSearchText(const std::string& name,
                                   const std::string& path,
                                   const std::string& source,
                                   const std::string& lastLLM);

bool ProjectMatchesSearch(const std::string& name,
                          const std::string& path,
                          const std::string& source,
                          const std::string& lastLLM,
                          const std::string& query);
