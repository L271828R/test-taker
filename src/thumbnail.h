#pragma once
#include <string>

// Returns the path to a JPEG thumbnail (max 400px on longest side) stored at
// {dir}/thumbs/{stem}.jpg next to the original.  Generates it on first call;
// returns the cached path on subsequent calls.  Falls back to originalPath on
// any error so callers always get a usable path.
std::string EnsureThumb(const std::string& originalPath);
