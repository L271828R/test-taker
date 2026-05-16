// tests/test_renderer.cpp
// Built via CMake: cmake --build build --target test_mdviewer && ./build/test_mdviewer

#include "html_template.h"
#include "markdown.h"
#include <iostream>
#include <fstream>
#include <string>

int main() {
    int failures = 0;

    // 1. DOCTYPE must be well-formed — a stray ')' puts the browser in quirks
    //    mode, breaking CSS custom-property inheritance and dark-mode colours.
    {
        std::string html = BuildHTML("", "test", false, 100);
        if (html.find("<!DOCTYPE html>") == std::string::npos) {
            std::cerr << "FAIL [doctype]: expected '<!DOCTYPE html>' — got: "
                      << html.substr(0, html.find('\n')) << "\n";
            ++failures;
        } else {
            std::cout << "PASS [doctype]\n";
        }
    }

    // 2. Dark mode: <html class="dark"> must be present so .dark CSS rules fire.
    {
        std::string html = BuildHTML("", "test", true, 100);
        if (html.find("<html lang=\"en\" class=\"dark\">") == std::string::npos) {
            std::cerr << "FAIL [dark-class]: '<html lang=\"en\" class=\"dark\">' not found\n";
            ++failures;
        } else {
            std::cout << "PASS [dark-class]\n";
        }
    }

    // 3. Light mode must NOT carry the dark class.
    {
        std::string html = BuildHTML("", "test", false, 100);
        if (html.find("class=\"dark\"") != std::string::npos) {
            std::cerr << "FAIL [light-no-dark-class]: class=\"dark\" found in light-mode HTML\n";
            ++failures;
        } else {
            std::cout << "PASS [light-no-dark-class]\n";
        }
    }

    // 4. Font size is injected into the CSS.
    {
        std::string html = BuildHTML("", "test", false, 120);
        if (html.find("19.2px") == std::string::npos) {
            std::cerr << "FAIL [font-size]: expected '19.2px' for 120% not found\n";
            ++failures;
        } else {
            std::cout << "PASS [font-size]\n";
        }
    }

    // 5. GetLLMReadme() covers the tidbit extension and core syntax.
    {
        std::string readme = GetLLMReadme();
        bool hasTidbit  = readme.find(":::tidbit") != std::string::npos;
        bool hasMermaid = readme.find("mermaid")   != std::string::npos;
        bool hasHeading = readme.find("# ")        != std::string::npos;
        if (!hasTidbit || !hasMermaid || !hasHeading) {
            std::cerr << "FAIL [llm-readme]: missing tidbit=" << hasTidbit
                      << " mermaid=" << hasMermaid << " heading=" << hasHeading << "\n";
            ++failures;
        } else {
            std::cout << "PASS [llm-readme]\n";
        }
    }

    // 6. :::tidbit[Speaker] block renders as a <details> widget.
    {
        std::string md =
            ":::tidbit[Bjarne Stroustrup]\n"
            "vtables are a feature, not a bug.\n"
            ":::\n";
        std::string html = RenderMarkdown(md);
        bool hasDetails = html.find("<details class=\"tidbit\">") != std::string::npos;
        bool hasSpeaker = html.find("Bjarne Stroustrup") != std::string::npos;
        bool hasContent = html.find("vtables are a feature") != std::string::npos;
        if (!hasDetails || !hasSpeaker || !hasContent) {
            std::cerr << "FAIL [tidbit]: expected <details class=\"tidbit\"> with speaker and content\n"
                      << "  got: " << html << "\n";
            ++failures;
        } else {
            std::cout << "PASS [tidbit]\n";
        }
    }

    // 7. Copy button CSS is emitted (.copy-btn class present).
    {
        std::string html = BuildHTML("", "test", false, 100);
        if (html.find(".copy-btn") == std::string::npos) {
            std::cerr << "FAIL [copy-btn-css]: '.copy-btn' not found in HTML output\n";
            ++failures;
        } else {
            std::cout << "PASS [copy-btn-css]\n";
        }
    }

    // 8. Copy button JS sends via the clipboardCopy message handler.
    {
        std::string html = BuildHTML("", "test", false, 100);
        if (html.find("clipboardCopy") == std::string::npos) {
            std::cerr << "FAIL [copy-btn-js]: 'clipboardCopy' handler not found in HTML output\n";
            ++failures;
        } else {
            std::cout << "PASS [copy-btn-js]\n";
        }
    }

    // 9. GetLLMReadme() documents the copy button and text-vs-code fence rule.
    {
        std::string readme = GetLLMReadme();
        bool hasCopy     = readme.find("Copy") != std::string::npos;
        bool hasTextRule = readme.find("no code") != std::string::npos;
        bool hasLangTable= readme.find("bash") != std::string::npos;
        if (!hasCopy || !hasTextRule || !hasLangTable) {
            std::cerr << "FAIL [llm-copy]: --llm output missing copy docs, "
                         "text-fence rule, or language table\n";
            ++failures;
        } else {
            std::cout << "PASS [llm-copy]\n";
        }
    }

    // 10. Copy button must NOT be appended inside <pre> — doing so causes WebKit
    //     to re-parse the element and strip hljs highlight spans. The button must
    //     go into a wrapper div that sits around the <pre>, not inside it.
    {
        std::string html = BuildHTML("", "test", false, 100);
        if (html.find("block.parentElement.appendChild") != std::string::npos) {
            std::cerr << "FAIL [copy-btn-outside-pre]: copy button is appended inside <pre>; "
                         "use a wrapper div to avoid breaking hljs highlighting\n";
            ++failures;
        } else {
            std::cout << "PASS [copy-btn-outside-pre]\n";
        }
    }

    // 11. hljs actually highlights Python code — run the bundled highlight.min.js
    //     via Node and verify keyword/string spans appear on a hello-world block.
    {
        int rc = std::system(
            "node tests/test_hljs.js highlight.min.js 2>&1"
        );
        if (rc != 0) {
            std::cerr << "FAIL [hljs-python]: syntax highlighting test failed "
                         "(see output above)\n";
            ++failures;
        } else {
            std::cout << "PASS [hljs-python]\n";
        }
    }

    // 12. GetString() must be used for script message payloads, not GetURL().
    //     GetURL() returns the page URL (file:///...), not the postMessage value.
    {
        std::ifstream src("src/mdviewer.cpp");
        std::string code((std::istreambuf_iterator<char>(src)),
                          std::istreambuf_iterator<char>());
        bool usesGetString = code.find("evt.GetString()") != std::string::npos;
        bool usesGetURLForClipboard =
            code.find("clipboardCopy") != std::string::npos &&
            code.find("SetData(new wxTextDataObject(evt.GetURL()") != std::string::npos;
        if (!usesGetString || usesGetURLForClipboard) {
            std::cerr << "FAIL [getstring-not-geturl]: script message payload must be "
                         "read via GetString(), not GetURL() (GetURL returns the page URL)\n";
            ++failures;
        } else {
            std::cout << "PASS [getstring-not-geturl]\n";
        }
    }

    // 13. Edit menu wires up copy via m_webView->Copy().
    {
        std::ifstream src("src/mdviewer.cpp");
        std::string code((std::istreambuf_iterator<char>(src)),
                          std::istreambuf_iterator<char>());
        bool hasCopy    = code.find("m_webView->Copy()") != std::string::npos;
        bool hasCopyId  = code.find("wxID_COPY") != std::string::npos;
        if (!hasCopy || !hasCopyId) {
            std::cerr << "FAIL [edit-copy]: expected m_webView->Copy() and wxID_COPY "
                         "in mdviewer.cpp\n";
            ++failures;
        } else {
            std::cout << "PASS [edit-copy]\n";
        }
    }

    // 14. Find in page uses m_webView->Find() — not a JS workaround.
    {
        std::ifstream src("src/mdviewer.cpp");
        std::string code((std::istreambuf_iterator<char>(src)),
                          std::istreambuf_iterator<char>());
        bool hasFind   = code.find("m_webView->Find(") != std::string::npos;
        bool hasFindId = code.find("wxID_FIND") != std::string::npos;
        if (!hasFind || !hasFindId) {
            std::cerr << "FAIL [find-in-page]: expected m_webView->Find( and wxID_FIND "
                         "in mdviewer.cpp\n";
            ++failures;
        } else {
            std::cout << "PASS [find-in-page]\n";
        }
    }

    // 15. Paste to render reads clipboard via wxTheClipboard.
    {
        std::ifstream src("src/mdviewer.cpp");
        std::string code((std::istreambuf_iterator<char>(src)),
                          std::istreambuf_iterator<char>());
        bool hasPaste   = code.find("wxID_PASTE") != std::string::npos;
        bool hasClipGet = code.find("wxTheClipboard->GetData") != std::string::npos;
        if (!hasPaste || !hasClipGet) {
            std::cerr << "FAIL [paste-render]: expected wxID_PASTE and "
                         "wxTheClipboard->GetData in mdviewer.cpp\n";
            ++failures;
        } else {
            std::cout << "PASS [paste-render]\n";
        }
    }

    // 16. Find bar must report the correct match count for a known document.
    //
    //     Scenario: markdown with three "apple" entries and two other fruits.
    //     The rendered visible text therefore contains exactly 3 "apple" words.
    //     wxWebView::Find() returns -1 on macOS WKWebView (its find API is async),
    //     so DoFind must obtain the total count via RunScript instead of using
    //     the Find() return value.
    //
    //     Part A: verify the markdown renders to exactly 3 visible "apple" words
    //             (i.e. the document is what we think it is).
    //     Part B: verify DoFind uses RunScript to count matches rather than
    //             trusting the Find() return value (which is always -1 on macOS).
    {
        // Part A — rendered text count
        std::string md =
            "# Fruits\n\n"
            "- apple\n"
            "- banana\n"
            "- apple\n"
            "- cherry\n"
            "- apple\n";
        std::string body = RenderMarkdown(md);

        // Strip HTML tags to get the visible text a browser would show.
        std::string visible;
        bool inTag = false;
        for (char c : body) {
            if      (c == '<') { inTag = true;  continue; }
            else if (c == '>') { inTag = false; continue; }
            else if (!inTag)   visible += c;
        }
        int appleCount = 0;
        for (size_t p = 0; (p = visible.find("apple", p)) != std::string::npos; ++p)
            ++appleCount;

        // Part B — DoFind must use RunScript for the count
        std::ifstream src("src/mdviewer.cpp");
        std::string code((std::istreambuf_iterator<char>(src)),
                          std::istreambuf_iterator<char>());
        bool usesRunScript = code.find("RunScript") != std::string::npos;

        if (appleCount != 3 || !usesRunScript) {
            std::cerr << "FAIL [find-count]: "
                      << "visible 'apple' count=" << appleCount << " (expected 3); "
                      << "DoFind uses RunScript=" << usesRunScript
                      << " — wxWebView::Find() returns -1 on macOS WKWebView so the "
                         "count must come from RunScript, not the Find() return value\n";
            ++failures;
        } else {
            std::cout << "PASS [find-count]\n";
        }
    }

    std::cout << (failures ? "FAILED" : "ALL PASSED") << "\n";
    return failures > 0 ? 1 : 0;
}
