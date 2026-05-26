// tests/test_persona_tab.cpp
// Tests for persona utilities and HTML — no wxWidgets dependency.

#include "persona.h"
#include "persona_tab_html.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

int test_persona_tab() {
    int failures = 0;

    // ── [persona-normalize] ──────────────────────────────────────────────────
    {
        struct { const char* in; const char* out; } cases[] = {
            {"Albert Einstein", "albert_einstein"},
            {"Marie Curie",     "marie_curie"},
            {"NIKOLA TESLA",    "nikola_tesla"},
            {"Ada  Lovelace",   "ada_lovelace"},
            {"trailing_",       "trailing"},
            {"",                ""},
        };
        for (auto& c : cases) {
            std::string got = NormalizePersonaName(c.in);
            if (got != c.out) {
                std::cerr << "FAIL [persona-normalize] in='" << c.in
                          << "' want='" << c.out << "' got='" << got << "'\n";
                ++failures;
            } else {
                std::cout << "PASS [persona-normalize-" << c.in << "]\n";
            }
        }
    }

    // ── [persona-scan-from-dir] ──────────────────────────────────────────────
    // ScanPersonaImages(dir) scans a caller-supplied directory instead of the
    // default ~/test-taker/personas so the function is testable without wx.
    {
        // Minimal 1×1 white PNG (valid header so extLower("png") matches)
        static const unsigned char kPng[] = {
            0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,
            0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
            0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
            0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
            0xde,0x00,0x00,0x00,0x0c,0x49,0x44,0x41,
            0x54,0x08,0xd7,0x63,0xf8,0xcf,0xc0,0x00,
            0x00,0x00,0x02,0x00,0x01,0xe2,0x21,0xbc,
            0x33,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,
            0x44,0xae,0x42,0x60,0x82
        };
        fs::path tmp = fs::temp_directory_path() / "tt_persona_scan_test";
        fs::create_directories(tmp);
        {
            std::ofstream f(tmp / "albert_einstein.png", std::ios::binary);
            f.write(reinterpret_cast<const char*>(kPng), sizeof(kPng));
        }
        auto result = ScanPersonaImages(tmp.string());
        bool hasKey  = result.count("albert_einstein") > 0;
        bool noThumb = result.count("thumbs") == 0;
        if (!hasKey || !noThumb) {
            std::cerr << "FAIL [persona-scan-from-dir]: hasKey=" << hasKey
                      << " noThumb=" << noThumb << "\n";
            ++failures;
        } else {
            std::cout << "PASS [persona-scan-from-dir]\n";
        }
        fs::remove_all(tmp);
    }

    // ── [persona-tab-html-basics] ────────────────────────────────────────────
    {
        std::string html = BuildPersonaTabHTML(false);
        bool ok = contains(html, "personas") &&
                  contains(html, "Guest Commentators") &&
                  contains(html, "setCharacters");
        if (!ok) {
            std::cerr << "FAIL [persona-tab-html-basics]: missing expected content\n";
            ++failures;
        } else {
            std::cout << "PASS [persona-tab-html-basics]\n";
        }
    }

    // ── [persona-tab-html-dark] ──────────────────────────────────────────────
    {
        std::string html = BuildPersonaTabHTML(true);
        bool ok = contains(html, "class=\"dark\"") || contains(html, "class='dark'");
        if (!ok) {
            std::cerr << "FAIL [persona-tab-html-dark]: no dark class\n";
            ++failures;
        } else {
            std::cout << "PASS [persona-tab-html-dark]\n";
        }
    }

    // ── [persona-tab-html-light] ─────────────────────────────────────────────
    {
        std::string html = BuildPersonaTabHTML(false);
        bool ok = !contains(html, "class=\"dark\"") && !contains(html, "class='dark'");
        if (!ok) {
            std::cerr << "FAIL [persona-tab-html-light]: dark class present in light mode\n";
            ++failures;
        } else {
            std::cout << "PASS [persona-tab-html-light]\n";
        }
    }

    // ── [persona-tab-html-flush-descs] ───────────────────────────────────────
    // C++ calls flushDescs() via RunScript when the notebook leaves this tab.
    // onblur on textarea is unreliable when focus moves to native wx controls.
    {
        std::string html = BuildPersonaTabHTML(false);
        bool hasFn        = contains(html, "function flushDescs");
        bool hasClear     = contains(html, "clearTimeout(_descTimers[name])");
        bool callsSaveDesc = contains(html, "saveDesc(name,");
        if (!hasFn || !hasClear || !callsSaveDesc) {
            std::cerr << "FAIL [persona-tab-html-flush-descs]:"
                      << " hasFn=" << hasFn
                      << " hasClear=" << hasClear
                      << " callsSaveDesc=" << callsSaveDesc << "\n";
            ++failures;
        } else {
            std::cout << "PASS [persona-tab-html-flush-descs]\n";
        }
    }

    // ── [persona-tab-html-save-desc-action] ──────────────────────────────────
    // saveDesc must send action:'setDesc' with name and description fields so
    // C++ HandleMessage can dispatch to DoSetDesc and persist via wxConfig.
    {
        std::string html = BuildPersonaTabHTML(false);
        bool hasAction = contains(html, "action:'setDesc'");
        bool hasName   = contains(html, "name:name");
        bool hasDesc   = contains(html, "description:desc");
        if (!hasAction || !hasName || !hasDesc) {
            std::cerr << "FAIL [persona-tab-html-save-desc-action]:"
                      << " hasAction=" << hasAction
                      << " hasName=" << hasName
                      << " hasDesc=" << hasDesc << "\n";
            ++failures;
        } else {
            std::cout << "PASS [persona-tab-html-save-desc-action]\n";
        }
    }

    // ── [persona-tab-html-schedule-desc] ─────────────────────────────────────
    // scheduleDesc must update _descs[name] immediately (in-memory) so
    // flushDescs can read the latest value even if the timer hasn't fired.
    {
        std::string html = BuildPersonaTabHTML(false);
        bool updatesDescs = contains(html, "_descs[name] = desc");
        bool setsTimer    = contains(html, "_descTimers[name] = setTimeout");
        if (!updatesDescs || !setsTimer) {
            std::cerr << "FAIL [persona-tab-html-schedule-desc]:"
                      << " updatesDescs=" << updatesDescs
                      << " setsTimer=" << setsTimer << "\n";
            ++failures;
        } else {
            std::cout << "PASS [persona-tab-html-schedule-desc]\n";
        }
    }

    return failures;
}
