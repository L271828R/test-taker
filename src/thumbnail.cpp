#include "thumbnail.h"
#include <algorithm>
#include <filesystem>
#include <wx/image.h>
#include <wx/string.h>

namespace fs = std::filesystem;

static constexpr int kMaxDim = 400;

std::string EnsureThumb(const std::string& originalPath) {
    std::error_code ec;
    fs::path orig(originalPath);

    fs::path thumbDir  = orig.parent_path() / "thumbs";
    fs::path thumbPath = thumbDir / (orig.stem().string() + ".jpg");

    // Return existing thumb if it is at least as new as the original.
    if (fs::exists(thumbPath, ec)) {
        auto tOrig  = fs::last_write_time(orig,      ec);
        if (ec) return thumbPath.string();
        auto tThumb = fs::last_write_time(thumbPath, ec);
        if (ec) return thumbPath.string();
        if (tThumb >= tOrig) return thumbPath.string();
    }

    // Register image handlers once.
    static bool sInit = false;
    if (!sInit) { wxInitAllImageHandlers(); sInit = true; }

    wxImage img;
    if (!img.LoadFile(wxString::FromUTF8(originalPath)))
        return originalPath;

    int w = img.GetWidth(), h = img.GetHeight();
    if (w > kMaxDim || h > kMaxDim) {
        if (w >= h) { h = std::max(1, h * kMaxDim / w); w = kMaxDim; }
        else        { w = std::max(1, w * kMaxDim / h); h = kMaxDim; }
        img.Rescale(w, h, wxIMAGE_QUALITY_HIGH);
    }

    fs::create_directories(thumbDir, ec);
    img.SetOption(wxIMAGE_OPTION_QUALITY, 85);
    if (!img.SaveFile(wxString::FromUTF8(thumbPath.string()), wxBITMAP_TYPE_JPEG))
        return originalPath;

    return thumbPath.string();
}
