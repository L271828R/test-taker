#include "../src/thumbnail.h"
// No-op stub for the test build: no wx dependency, just return the original path.
std::string EnsureThumb(const std::string& path) { return path; }
