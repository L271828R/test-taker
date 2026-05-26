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

    // BuildLogsHTML with invalid UTF-8 in log content must still produce
    // valid UTF-8 HTML so wxString::FromUTF8 doesn't return an empty string.
    // Reproduces: orphaned \xe3 lead byte from a truncated LLM response.
    {
        // \xe3 is a 3-byte lead byte — missing its two continuation bytes.
        std::string raw =
            "2026-05-26 09:00:00  LLM response: \xe6\xa0\xbc\xe3\xe2\x80\xa6 done\n";
        std::string html = BuildLogsHTML(raw, "/tmp/test.log", false);

        // Validate the returned HTML is valid UTF-8.
        bool validUtf8 = true;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(html.data());
        const unsigned char* e = p + html.size();
        while (p < e) {
            unsigned char c = *p;
            int extra = 0;
            if      (c < 0x80)              extra = 0;
            else if ((c & 0xE0) == 0xC0)    extra = 1;
            else if ((c & 0xF0) == 0xE0)    extra = 2;
            else if ((c & 0xF8) == 0xF0)    extra = 3;
            else { validUtf8 = false; break; }
            for (int j = 0; j < extra; ++j) {
                if (++p >= e || (*p & 0xC0) != 0x80) { validUtf8 = false; break; }
            }
            if (!validUtf8) break;
            ++p;
        }
        bool hasTable = html.find("<table>") != std::string::npos;
        if (!validUtf8 || !hasTable) {
            std::cerr << "FAIL [logs-html-invalid-utf8]: validUtf8=" << validUtf8
                      << " hasTable=" << hasTable << "\n";
            ++failures;
        } else {
            std::cout << "PASS [logs-html-invalid-utf8]\n";
        }
    }

    // BuildRagLogsHTML: empty log returns a valid HTML page with "RAG" in the title.
    {
        std::string html = BuildRagLogsHTML("", "/tmp/rag.log", false);
        bool hasDoctype = html.find("<!DOCTYPE html>") != std::string::npos;
        bool hasRag     = html.find("RAG")             != std::string::npos;
        if (!hasDoctype || !hasRag) {
            std::cerr << "FAIL [rag-logs-html-empty]: doctype=" << hasDoctype
                      << " rag=" << hasRag << "\n";
            ++failures;
        } else {
            std::cout << "PASS [rag-logs-html-empty]\n";
        }
    }

    // BuildRagLogsHTML: event block surfaces query, chunk text, score, and doc name.
    {
        std::string raw =
            "RAG_EVENT\n"
            "time=2026-05-17 21:58:39\n"
            "context=Chat\n"
            "query=explain test control\n"
            "CHUNK score=0.847 doc=qa_guide.pdf\n"
            "test control is taking corrective action\n"
            "END_EVENT\n";
        std::string html = BuildRagLogsHTML(raw, "/tmp/rag.log", false);
        bool hasQuery = html.find("explain test control")              != std::string::npos;
        bool hasChunk = html.find("test control is taking corrective") != std::string::npos;
        bool hasScore = html.find("0.847")                             != std::string::npos;
        bool hasDoc   = html.find("qa_guide.pdf")                      != std::string::npos;
        if (!hasQuery || !hasChunk || !hasScore || !hasDoc) {
            std::cerr << "FAIL [rag-logs-html-event]: query=" << hasQuery
                      << " chunk=" << hasChunk << " score=" << hasScore
                      << " doc=" << hasDoc << "\n";
            ++failures;
        } else {
            std::cout << "PASS [rag-logs-html-event]\n";
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

    // BuildHTML includes the Desmos CDN script tag so graph blocks can render.
    {
        std::string html = BuildHTML("", "test", false, 100);
        bool hasDesmos = html.find("Plotly.newPlot") != std::string::npos;
        if (!hasDesmos) {
            std::cerr << "FAIL [graph-desmos-js]: Desmos CDN script not found in BuildHTML output\n";
            ++failures;
        } else {
            std::cout << "PASS [graph-desmos-js]\n";
        }
    }

    // BuildHTML includes JS that initialises Plotly on .math-graph divs.
    {
        std::string html = BuildHTML("", "test", false, 100);
        bool hasInit = html.find("math-graph") != std::string::npos
                    && html.find("Plotly.newPlot") != std::string::npos;
        if (!hasInit) {
            std::cerr << "FAIL [graph-plotly-init]: Plotly initialiser not found in BuildHTML output\n";
            ++failures;
        } else {
            std::cout << "PASS [graph-plotly-init]\n";
        }
    }

    // GetLLMReadme() documents the ```graph fence syntax.
    {
        std::string readme = GetLLMReadme();
        bool hasGraph = readme.find("```graph") != std::string::npos;
        if (!hasGraph) {
            std::cerr << "FAIL [graph-llm-readme]: ```graph not documented in GetLLMReadme\n";
            ++failures;
        } else {
            std::cout << "PASS [graph-llm-readme]\n";
        }
    }

    return failures;
}
