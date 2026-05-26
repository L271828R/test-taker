#include "persona.h"
#include "thumbnail.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <vector>

std::string GetPersonasDir() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/test-taker/personas" : "";
}

std::string NormalizePersonaName(const std::string& name) {
    std::string out;
    const unsigned char* p   = reinterpret_cast<const unsigned char*>(name.c_str());
    const unsigned char* end = p + name.size();
    while (p < end) {
        unsigned char c = *p;
        if (c < 0x80) {
            if (std::isalnum(c))
                out += (char)std::tolower(c);
            else if (std::isspace(c) || c == '_') {
                if (!out.empty() && out.back() != '_')
                    out += '_';
            }
            ++p;
        } else {
            int bytes = (c & 0xF8) == 0xF0 ? 4 :
                        (c & 0xF0) == 0xE0 ? 3 :
                        (c & 0xE0) == 0xC0 ? 2 : 1;
            for (int i = 0; i < bytes && p < end; ++i, ++p)
                out += (char)*p;
        }
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    return out;
}

static std::string extLower(const std::string& filename) {
    auto dot = filename.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return ext;
}

static std::string stemOf(const std::string& filename) {
    auto dot = filename.rfind('.');
    return dot == std::string::npos ? filename : filename.substr(0, dot);
}

static bool isImageExt(const std::string& ext) {
    return ext == "jpg" || ext == "jpeg" || ext == "png"
        || ext == "gif" || ext == "webp";
}

std::map<std::string, std::string> ScanPersonaImages(const std::string& dir) {
    std::map<std::string, std::string> result;
    if (dir.empty()) return result;

    DIR* d = opendir(dir.c_str());
    if (!d) return result;

    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        std::string filename(entry->d_name);
        std::string ext = extLower(filename);
        if (!isImageExt(ext)) continue;
        std::string norm = NormalizePersonaName(stemOf(filename));
        if (norm.empty()) continue;
        std::string thumbPath = EnsureThumb(dir + "/" + filename);
        result[norm] = "file://" + thumbPath;
    }
    closedir(d);
    return result;
}

std::map<std::string, std::string> ScanPersonaImages() {
    return ScanPersonaImages(GetPersonasDir());
}

static std::string base64Encode(const std::vector<uint8_t>& d) {
    static const char* T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((d.size() + 2) / 3) * 4);
    for (size_t i = 0; i < d.size(); i += 3) {
        uint32_t n = (uint32_t)d[i] << 16;
        if (i+1 < d.size()) n |= (uint32_t)d[i+1] << 8;
        if (i+2 < d.size()) n |= (uint32_t)d[i+2];
        out += T[(n>>18)&63]; out += T[(n>>12)&63];
        out += (i+1 < d.size()) ? T[(n>>6)&63] : '=';
        out += (i+2 < d.size()) ? T[n&63]      : '=';
    }
    return out;
}

static std::string fileToDataUrl(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    if (data.empty()) return "";
    std::string ext = extLower(path);
    std::string mime = (ext == "png")  ? "image/png"  :
                       (ext == "gif")  ? "image/gif"  :
                       (ext == "webp") ? "image/webp" : "image/jpeg";
    return "data:" + mime + ";base64," + base64Encode(data);
}

std::map<std::string, std::string> ToDataURLs(
    const std::map<std::string, std::string>& fileUrls)
{
    std::map<std::string, std::string> result;
    for (const auto& kv : fileUrls) {
        const std::string& url = kv.second;
        std::string path = (url.size() >= 7 && url.substr(0, 7) == "file://")
                           ? url.substr(7) : url;
        std::string data = fileToDataUrl(path);
        if (!data.empty())
            result[kv.first] = data;
    }
    return result;
}

std::string AddPersonaImage(const std::string& personaName,
                             const std::string& srcImagePath) {
    std::string dir = GetPersonasDir();
    if (dir.empty()) return "";

    mkdir(dir.c_str(), 0755);

    std::string norm = NormalizePersonaName(personaName);
    if (norm.empty()) return "";

    std::string ext = extLower(srcImagePath);
    if (ext.empty()) ext = "jpg";

    std::string destPath = dir + "/" + norm + "." + ext;

    std::ifstream src(srcImagePath, std::ios::binary);
    std::ofstream dst(destPath, std::ios::binary | std::ios::trunc);
    if (!src || !dst) return "";
    dst << src.rdbuf();
    return destPath;
}

bool RenamePersonaImage(const std::string& oldName,
                        const std::string& newName,
                        const std::string& personasDir) {
    std::string dir = personasDir.empty() ? GetPersonasDir() : personasDir;
    if (dir.empty()) return false;

    std::string oldNorm = NormalizePersonaName(oldName);
    std::string newNorm = NormalizePersonaName(newName);
    if (oldNorm.empty() || newNorm.empty()) return false;

    static const char* exts[] = {"jpg", "jpeg", "png", "gif", "webp", nullptr};
    for (int i = 0; exts[i]; ++i) {
        std::string srcPath = dir + "/" + oldNorm + "." + exts[i];
        struct stat st;
        if (::stat(srcPath.c_str(), &st) != 0) continue;
        std::string dstPath = dir + "/" + newNorm + "." + exts[i];
        return ::rename(srcPath.c_str(), dstPath.c_str()) == 0;
    }
    return false;
}
