#pragma once
#include <string>

// Strip HTML tags, script/style blocks, comments, and decode common entities.
// Returns plain text suitable for corpus ingestion.
std::string StripHTML(const std::string& html);

// Derive a short filename-safe slug from a URL (no scheme, no extension, max 60 chars).
// E.g. "https://google.github.io/googletest/primer.html" → "google-github-io-googletest-primer"
std::string NameFromURL(const std::string& url);

// Download the URL using curl and return the raw response body.
// Returns empty string on failure and sets err.
std::string FetchURL(const std::string& url, std::string& err);
