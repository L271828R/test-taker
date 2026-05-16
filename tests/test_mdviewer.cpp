#include "markdown.h"
#include <iostream>
#include <fstream>
#include <string>

int test_mdviewer() {
    int failures = 0;

    // GetString() must be used for script message payloads, not GetURL().
    // GetURL() returns the page URL (file:///...), not the postMessage value.
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

    // Edit menu wires up copy via m_webView->Copy().
    {
        std::ifstream src("src/mdviewer.cpp");
        std::string code((std::istreambuf_iterator<char>(src)),
                          std::istreambuf_iterator<char>());
        bool hasCopy   = code.find("m_webView->Copy()") != std::string::npos;
        bool hasCopyId = code.find("wxID_COPY") != std::string::npos;
        if (!hasCopy || !hasCopyId) {
            std::cerr << "FAIL [edit-copy]: expected m_webView->Copy() and wxID_COPY "
                         "in mdviewer.cpp\n";
            ++failures;
        } else {
            std::cout << "PASS [edit-copy]\n";
        }
    }

    // Find in page uses m_webView->Find() — not a JS workaround.
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

    // Paste to render reads clipboard via wxTheClipboard.
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

    // Find bar must report the correct match count for a known document.
    // wxWebView::Find() returns -1 on macOS WKWebView (async under the hood),
    // so DoFind must obtain the total count via RunScript instead.
    {
        std::ifstream src("src/mdviewer.cpp");
        std::string code((std::istreambuf_iterator<char>(src)),
                          std::istreambuf_iterator<char>());
        bool usesRunScript = code.find("RunScript") != std::string::npos;
        if (!usesRunScript) {
            std::cerr << "FAIL [find-count]: DoFind must count via RunScript/JS, not "
                         "wxWebView::Find() return value (unreliable on macOS WKWebView)\n";
            ++failures;
        } else {
            std::cout << "PASS [find-count]\n";
        }
    }

    return failures;
}
