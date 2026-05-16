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

    return failures;
}
