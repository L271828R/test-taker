#pragma once
#include <map>
#include <string>
#include <vector>

// Returns the built-in personality library used when no saved library exists.
std::map<std::string, std::vector<std::string>> DefaultPersonalityLibrary();
