#include "markdown.h"
#include <iostream>
#include <string>

int test_markdown() {
    int failures = 0;

    // :::tidbit[Speaker] block renders as a <details> widget.
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

    // Find-count scenario: fruit list renders with exactly 3 visible "apple" words.
    // (Part A of the find-count test — verifies the rendered text; Part B is in
    //  test_mdviewer.cpp which checks the DoFind implementation.)
    {
        std::string md =
            "# Fruits\n\n"
            "- apple\n"
            "- banana\n"
            "- apple\n"
            "- cherry\n"
            "- apple\n";
        std::string body = RenderMarkdown(md);

        std::string visible;
        bool inTag = false;
        for (char c : body) {
            if      (c == '<') { inTag = true;  continue; }
            else if (c == '>') { inTag = false; continue; }
            else if (!inTag)   visible += c;
        }
        int count = 0;
        for (size_t p = 0; (p = visible.find("apple", p)) != std::string::npos; ++p)
            ++count;

        if (count != 3) {
            std::cerr << "FAIL [fruit-render]: expected 3 visible 'apple' occurrences, got "
                      << count << "\n";
            ++failures;
        } else {
            std::cout << "PASS [fruit-render]\n";
        }
    }

    // Fenced code block renders as <pre><code class="language-X"> for hljs.
    {
        std::string md =
            "Here is an example:\n\n"
            "```cpp\n"
            "int x = 42;\n"
            "```\n";
        std::string html = RenderMarkdown(md);
        bool hasPre      = html.find("<pre>") != std::string::npos;
        bool hasLangClass = html.find("language-cpp") != std::string::npos;
        bool hasCode     = html.find("int x = 42") != std::string::npos;
        if (!hasPre || !hasLangClass || !hasCode) {
            std::cerr << "FAIL [fenced-code-block]:"
                      << " pre=" << hasPre
                      << " lang=" << hasLangClass
                      << " code=" << hasCode << "\n"
                      << "  html: " << html << "\n";
            ++failures;
        } else {
            std::cout << "PASS [fenced-code-block]\n";
        }
    }

    // Fenced code block without language tag still renders as <pre><code>.
    {
        std::string md = "```\nplain code\n```\n";
        std::string html = RenderMarkdown(md);
        bool hasPre  = html.find("<pre>") != std::string::npos;
        bool hasCode = html.find("plain code") != std::string::npos;
        if (!hasPre || !hasCode) {
            std::cerr << "FAIL [fenced-code-no-lang]: pre=" << hasPre
                      << " code=" << hasCode << "\n";
            ++failures;
        } else {
            std::cout << "PASS [fenced-code-no-lang]\n";
        }
    }

    // ```graph block with a single expression renders a desmos-graph div.
    {
        std::string md = "```graph\nx^{2}\n```\n";
        std::string html = RenderMarkdown(md);
        bool hasDiv   = html.find("class=\"math-graph\"") != std::string::npos;
        bool hasExprs = html.find("data-exprs=") != std::string::npos;
        bool hasExpr  = html.find("x^{2}") != std::string::npos;
        bool noPre    = html.find("<pre>") == std::string::npos;
        if (!hasDiv || !hasExprs || !hasExpr || !noPre) {
            std::cerr << "FAIL [graph-block-basic]: div=" << hasDiv
                      << " exprs=" << hasExprs << " expr=" << hasExpr
                      << " noPre=" << noPre << "\n  html: " << html << "\n";
            ++failures;
        } else {
            std::cout << "PASS [graph-block-basic]\n";
        }
    }

    // ```graph block with multiple expressions includes all of them.
    {
        std::string md = "```graph\nx^{2}\nx^{-3}\n```\n";
        std::string html = RenderMarkdown(md);
        bool hasDiv  = html.find("class=\"math-graph\"") != std::string::npos;
        bool hasF1   = html.find("x^{2}") != std::string::npos;
        bool hasF2   = html.find("x^{-3}") != std::string::npos;
        if (!hasDiv || !hasF1 || !hasF2) {
            std::cerr << "FAIL [graph-block-multi]: div=" << hasDiv
                      << " f1=" << hasF1 << " f2=" << hasF2
                      << "\n  html: " << html << "\n";
            ++failures;
        } else {
            std::cout << "PASS [graph-block-multi]\n";
        }
    }

    return failures;
}
