#include "html_template.h"
#include "markdown.h"
#include <iostream>
#include <string>

int test_html_template() {
    int failures = 0;

    // DOCTYPE must be well-formed — a stray ')' puts the browser in quirks
    // mode, breaking CSS custom-property inheritance and dark-mode colours.
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

    // Dark mode: <html class="dark"> must be present so .dark CSS rules fire.
    {
        std::string html = BuildHTML("", "test", true, 100);
        if (html.find("<html lang=\"en\" class=\"dark\">") == std::string::npos) {
            std::cerr << "FAIL [dark-class]: '<html lang=\"en\" class=\"dark\">' not found\n";
            ++failures;
        } else {
            std::cout << "PASS [dark-class]\n";
        }
    }

    // Light mode must NOT carry the dark class.
    {
        std::string html = BuildHTML("", "test", false, 100);
        if (html.find("class=\"dark\"") != std::string::npos) {
            std::cerr << "FAIL [light-no-dark-class]: class=\"dark\" found in light-mode HTML\n";
            ++failures;
        } else {
            std::cout << "PASS [light-no-dark-class]\n";
        }
    }

    // Font size is injected into the CSS.
    {
        std::string html = BuildHTML("", "test", false, 120);
        if (html.find("19.2px") == std::string::npos) {
            std::cerr << "FAIL [font-size]: expected '19.2px' for 120% not found\n";
            ++failures;
        } else {
            std::cout << "PASS [font-size]\n";
        }
    }

    // GetLLMReadme() covers the tidbit extension and core syntax.
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

    // Copy button CSS is emitted (.copy-btn class present).
    {
        std::string html = BuildHTML("", "test", false, 100);
        if (html.find(".copy-btn") == std::string::npos) {
            std::cerr << "FAIL [copy-btn-css]: '.copy-btn' not found in HTML output\n";
            ++failures;
        } else {
            std::cout << "PASS [copy-btn-css]\n";
        }
    }

    // Copy button JS sends via the clipboardCopy message handler.
    {
        std::string html = BuildHTML("", "test", false, 100);
        if (html.find("clipboardCopy") == std::string::npos) {
            std::cerr << "FAIL [copy-btn-js]: 'clipboardCopy' handler not found in HTML output\n";
            ++failures;
        } else {
            std::cout << "PASS [copy-btn-js]\n";
        }
    }

    // GetLLMReadme() documents the copy button and text-vs-code fence rule.
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

    // Copy button must NOT be appended inside <pre> — doing so causes WebKit
    // to re-parse the element and strip hljs highlight spans.
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

    // hljs actually highlights Python code — run the bundled highlight.min.js
    // via Node and verify keyword/string spans appear on a hello-world block.
    {
        int rc = std::system("node tests/test_hljs.js highlight.min.js 2>&1");
        if (rc != 0) {
            std::cerr << "FAIL [hljs-python]: syntax highlighting test failed "
                         "(see output above)\n";
            ++failures;
        } else {
            std::cout << "PASS [hljs-python]\n";
        }
    }

    // BuildLogsHTML wraps log lines in a themed HTML table.
    {
        std::string raw =
            "2026-05-10 12:00:00  ReadFile OK: /tmp/test.md  (42 bytes)\n"
            "2026-05-10 12:00:01  ReadFile FAILED: /tmp/missing.md\n";
        std::string html = BuildLogsHTML(raw, "/tmp/mdviewer.log", false);
        bool hasDoctype = html.find("<!DOCTYPE html>") != std::string::npos;
        bool hasTable   = html.find("<table>")         != std::string::npos;
        bool hasOK      = html.find("ReadFile OK")     != std::string::npos;
        bool hasFailed  = html.find("ReadFile FAILED") != std::string::npos;
        if (!hasDoctype || !hasTable || !hasOK || !hasFailed) {
            std::cerr << "FAIL [logs-html]: BuildLogsHTML missing expected content: "
                      << "doctype=" << hasDoctype << " table=" << hasTable
                      << " ok-line=" << hasOK << " failed-line=" << hasFailed << "\n";
            ++failures;
        } else {
            std::cout << "PASS [logs-html]\n";
        }
    }

    // Long CJK/no-space text should wrap inside the document column.
    {
        std::string html = BuildHTML("<p>很长很长很长很长很长很长很长很长很长很长</p>", "wrap", false, 100);
        bool bodyWrap = html.find("overflow-wrap:anywhere") != std::string::npos;
        bool wordBreak = html.find("word-break:break-word") != std::string::npos;
        if (!bodyWrap || !wordBreak) {
            std::cerr << "FAIL [cjk-wrap-css]: wrap CSS missing\n";
            ++failures;
        } else {
            std::cout << "PASS [cjk-wrap-css]\n";
        }
    }

    return failures;
}
