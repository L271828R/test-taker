#include "game_data.h"
#include <fstream>
#include <sstream>

static std::string encodeNL(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else                out += c;
    }
    return out;
}

static std::string decodeNL(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if      (s[i+1] == 'n')  { out += '\n'; ++i; }
            else if (s[i+1] == '\\') { out += '\\'; ++i; }
            else                     { out += s[i]; }
        } else {
            out += s[i];
        }
    }
    return out;
}

static void writeBlock(std::ofstream& f, const GameData& d) {
    f << "QUESTION: "     << encodeNL(d.question) << "\n";
    f << "CHOICE_A: "     << encodeNL(d.choiceA)  << "\n";
    f << "CHOICE_B: "     << encodeNL(d.choiceB)  << "\n";
    f << "CORRECT_IS_A: " << (d.correctIsA ? "true" : "false") << "\n";
}

static GameData parseBlock(const std::vector<std::string>& lines) {
    GameData d;
    for (const auto& line : lines) {
        if      (line.rfind("QUESTION: ",     0) == 0) d.question   = decodeNL(line.substr(10));
        else if (line.rfind("CHOICE_A: ",     0) == 0) d.choiceA    = decodeNL(line.substr(10));
        else if (line.rfind("CHOICE_B: ",     0) == 0) d.choiceB    = decodeNL(line.substr(10));
        else if (line.rfind("CORRECT_IS_A: ", 0) == 0) d.correctIsA = (line.substr(14) == "true");
    }
    return d;
}

bool WriteGameFile(const std::string& path, const GameData& d) {
    std::ofstream f(path);
    if (!f) return false;
    writeBlock(f, d);
    return f.good();
}

GameData ReadGameFile(const std::string& path) {
    auto items = ReadGameFiles(path);
    return items.empty() ? GameData{} : items[0];
}

bool WriteGameFiles(const std::string& path, const std::vector<GameData>& items) {
    std::ofstream f(path);
    if (!f) return false;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) f << "---\n";
        writeBlock(f, items[i]);
    }
    return f.good();
}

std::vector<GameData> ReadGameFiles(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::vector<GameData> result;
    std::vector<std::string> block;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "---") {
            if (!block.empty()) { result.push_back(parseBlock(block)); block.clear(); }
        } else {
            block.push_back(line);
        }
    }
    if (!block.empty()) result.push_back(parseBlock(block));
    return result;
}

bool AppendGameFiles(const std::string& path, const std::vector<GameData>& items) {
    if (items.empty()) return true;
    std::ofstream f(path, std::ios::app);
    if (!f) return false;
    for (const auto& d : items) {
        f << "---\n";
        writeBlock(f, d);
    }
    return f.good();
}
