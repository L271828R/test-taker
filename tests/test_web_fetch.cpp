#include "web_fetch.h"
#include <iostream>

int test_web_fetch() {
    int failures = 0;

    // StripHTML: plain text passes through unchanged
    {
        std::string out = StripHTML("Hello world");
        if (out != "Hello world") {
            std::cerr << "FAIL [strip-plain]: got '" << out << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [strip-plain]\n";
        }
    }

    // StripHTML: removes basic tags
    {
        std::string out = StripHTML("<h1>Title</h1><p>Body text.</p>");
        bool ok = out.find("Title") != std::string::npos
               && out.find("Body text.") != std::string::npos
               && out.find('<') == std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [strip-tags]: got '" << out << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [strip-tags]\n";
        }
    }

    // StripHTML: removes <script> block including its content
    {
        std::string out = StripHTML("<p>Keep</p><script>alert('bad')</script><p>this</p>");
        bool ok = out.find("Keep") != std::string::npos
               && out.find("this") != std::string::npos
               && out.find("alert") == std::string::npos
               && out.find('<') == std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [strip-script]: got '" << out << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [strip-script]\n";
        }
    }

    // StripHTML: removes <style> block including its content
    {
        std::string out = StripHTML("<style>body{color:red}</style><p>Text</p>");
        bool ok = out.find("Text") != std::string::npos
               && out.find("color") == std::string::npos
               && out.find('<') == std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [strip-style]: got '" << out << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [strip-style]\n";
        }
    }

    // StripHTML: decodes common HTML entities
    {
        std::string out = StripHTML("&lt;tag&gt; &amp; &quot;quote&quot; &nbsp;space");
        bool hasLt    = out.find('<') != std::string::npos;
        bool hasGt    = out.find('>') != std::string::npos;
        bool hasAmp   = out.find('&') != std::string::npos;
        bool hasQuote = out.find('"') != std::string::npos;
        bool noEntity = out.find("&lt;") == std::string::npos;
        if (!hasLt || !hasGt || !hasAmp || !hasQuote || !noEntity) {
            std::cerr << "FAIL [strip-entities]: got '" << out << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [strip-entities]\n";
        }
    }

    // StripHTML: removes HTML comments
    {
        std::string out = StripHTML("Before<!-- hidden comment -->After");
        bool ok = out.find("Before") != std::string::npos
               && out.find("After") != std::string::npos
               && out.find("hidden") == std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [strip-comments]: got '" << out << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [strip-comments]\n";
        }
    }

    // NameFromURL: basic URL → filename-safe slug
    {
        std::string name = NameFromURL("https://google.github.io/googletest/primer.html");
        bool ok = name.find("googletest") != std::string::npos
               && name.find("://") == std::string::npos
               && name.find('/') == std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [url-name-basic]: got '" << name << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [url-name-basic]\n";
        }
    }

    // NameFromURL: strips .html extension
    {
        std::string name = NameFromURL("https://example.com/page.html");
        bool ok = name.find(".html") == std::string::npos
               && name.find("page") != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [url-name-no-ext]: got '" << name << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [url-name-no-ext]\n";
        }
    }

    // NameFromURL: handles bare domain
    {
        std::string name = NameFromURL("https://example.com");
        bool ok = !name.empty()
               && name.find("://") == std::string::npos
               && name.find('/') == std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [url-name-domain]: got '" << name << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [url-name-domain]\n";
        }
    }

    // NameFromURL: truncates long URLs
    {
        std::string longUrl = "https://very-long-domain.example.com/very/deeply/nested/path/to/some/resource";
        std::string name = NameFromURL(longUrl);
        bool ok = name.size() <= 60
               && name.find("://") == std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [url-name-truncate]: len=" << name.size()
                      << " name='" << name << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [url-name-truncate]\n";
        }
    }

    return failures;
}
