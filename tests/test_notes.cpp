#include "notes.h"
#include "markdown.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

namespace fs = std::filesystem;

static fs::path make_temp_dir() {
    auto ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto p = fs::temp_directory_path() / ("notes_test_" + std::to_string(ns));
    fs::create_directories(p);
    return p;
}

int test_notes() {
    int failures = 0;

    // [notes-load-empty] — LoadNotes on a dir with no notes.json returns empty vector
    {
        auto tmp = make_temp_dir();
        auto result = LoadNotes(tmp.string());
        if (!result.empty()) {
            std::cerr << "FAIL [notes-load-empty]: expected empty, got "
                      << result.size() << " notes\n";
            ++failures;
        } else {
            std::cout << "PASS [notes-load-empty]\n";
        }
        fs::remove_all(tmp);
    }

    // [notes-save-roundtrip] — SaveNotes then LoadNotes round-trips all fields
    {
        auto tmp = make_temp_dir();
        std::vector<Note> notes;
        Note n;
        n.id           = 42;
        n.anchor       = "note:42";
        n.selectedText = "hello world";
        n.text         = "my note\nwith newline";
        n.file         = "ch01.md";
        notes.push_back(n);

        bool saved = SaveNotes(tmp.string(), notes);
        auto loaded = LoadNotes(tmp.string());

        bool ok = saved
               && loaded.size() == 1
               && loaded[0].id           == 42
               && loaded[0].anchor       == "note:42"
               && loaded[0].selectedText == "hello world"
               && loaded[0].text         == "my note\nwith newline"
               && loaded[0].file         == "ch01.md";
        if (!ok) {
            std::cerr << "FAIL [notes-save-roundtrip]: saved=" << saved
                      << " size=" << loaded.size();
            if (!loaded.empty()) {
                std::cerr << " id=" << loaded[0].id
                          << " anchor=" << loaded[0].anchor
                          << " sel=" << loaded[0].selectedText
                          << " text=" << loaded[0].text
                          << " file=" << loaded[0].file;
            }
            std::cerr << "\n";
            ++failures;
        } else {
            std::cout << "PASS [notes-save-roundtrip]\n";
        }
        fs::remove_all(tmp);
    }

    // [notes-next-id] — NextNoteId returns 1 for empty, max+1 for non-empty
    {
        std::vector<Note> empty;
        int id1 = NextNoteId(empty);

        std::vector<Note> notes;
        Note a; a.id = 3; notes.push_back(a);
        Note b; b.id = 7; notes.push_back(b);
        Note c; c.id = 2; notes.push_back(c);
        int id2 = NextNoteId(notes);

        bool ok = (id1 == 1) && (id2 == 8);
        if (!ok) {
            std::cerr << "FAIL [notes-next-id]: empty=" << id1 << " max+1=" << id2 << "\n";
            ++failures;
        } else {
            std::cout << "PASS [notes-next-id]\n";
        }
    }

    // [notes-insert-anchor] — InsertNoteAnchor inserts after selectedText within context
    {
        std::string md = "Once upon a time there was a fox that ran fast.\n";
        std::string selected = "fox";
        std::string context  = "a time there was a fox that ran";
        std::string result   = InsertNoteAnchor(md, selected, context, 3);

        bool hasMarker  = result.find("<!-- note:3 -->") != std::string::npos;
        bool afterFox   = result.find("fox<!-- note:3 -->") != std::string::npos;
        bool unchanged  = result.find("Once upon") != std::string::npos;
        bool noDouble   = result.find("fox<!-- note:3 --><!-- note:3 -->") == std::string::npos;
        if (!hasMarker || !afterFox || !unchanged || !noDouble) {
            std::cerr << "FAIL [notes-insert-anchor]: hasMarker=" << hasMarker
                      << " afterFox=" << afterFox
                      << " unchanged=" << unchanged
                      << "\n  result: '" << result << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [notes-insert-anchor]\n";
        }
    }

    // [notes-insert-anchor-fallback] — InsertNoteAnchor falls back to direct search
    {
        std::string md = "The quick brown fox jumps over the lazy dog.\n";
        std::string selected = "brown";
        std::string context  = "this context is not in the file at all";
        std::string result   = InsertNoteAnchor(md, selected, context, 5);

        bool hasMarker   = result.find("<!-- note:5 -->") != std::string::npos;
        bool afterBrown  = result.find("brown<!-- note:5 -->") != std::string::npos;
        if (!hasMarker || !afterBrown) {
            std::cerr << "FAIL [notes-insert-anchor-fallback]: hasMarker=" << hasMarker
                      << " afterBrown=" << afterBrown
                      << "\n  result: '" << result << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [notes-insert-anchor-fallback]\n";
        }
    }

    // [notes-remove-anchor] — RemoveNoteAnchor removes the marker, leaves rest unchanged
    {
        std::string md = "Hello world<!-- note:7 --> and goodbye<!-- note:3 -->.\n";
        std::string result = RemoveNoteAnchor(md, 7);

        bool markerGone  = result.find("<!-- note:7 -->") == std::string::npos;
        bool otherKept   = result.find("<!-- note:3 -->") != std::string::npos;
        bool helloKept   = result.find("Hello world") != std::string::npos;
        bool goodbyeKept = result.find("goodbye") != std::string::npos;
        if (!markerGone || !otherKept || !helloKept || !goodbyeKept) {
            std::cerr << "FAIL [notes-remove-anchor]: markerGone=" << markerGone
                      << " otherKept=" << otherKept
                      << " helloKept=" << helloKept
                      << " goodbyeKept=" << goodbyeKept
                      << "\n  result: '" << result << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [notes-remove-anchor]\n";
        }
    }

    // [notes-rescue-orphans] — RescueOrphanedNotes appends section and strips markers
    {
        std::string md =
            "# Chapter\n\n"
            "Some text<!-- note:1 --> here.\n\n"
            "More text<!-- note:2 -->.\n";

        std::vector<Note> notes;
        Note n1; n1.id = 1; n1.anchor = "note:1"; n1.selectedText = "text";
                 n1.text = "A note about text"; n1.file = "ch01.md";
        Note n2; n2.id = 2; n2.anchor = "note:2"; n2.selectedText = "More";
                 n2.text = "Another note"; n2.file = "ch01.md";
        notes.push_back(n1);
        notes.push_back(n2);

        std::string result = RescueOrphanedNotes(md, notes, "ch01.md");

        bool markersGone = result.find("<!-- note:1 -->") == std::string::npos
                        && result.find("<!-- note:2 -->") == std::string::npos;
        bool hasSection  = result.find("## Notes (from previous version)") != std::string::npos;
        bool hasNote1    = result.find("A note about text") != std::string::npos;
        bool hasNote2    = result.find("Another note") != std::string::npos;
        bool notesCleared = notes.empty();

        if (!markersGone || !hasSection || !hasNote1 || !hasNote2 || !notesCleared) {
            std::cerr << "FAIL [notes-rescue-orphans]: markersGone=" << markersGone
                      << " hasSection=" << hasSection
                      << " hasNote1=" << hasNote1
                      << " hasNote2=" << hasNote2
                      << " notesCleared=" << notesCleared
                      << "\n  result: '" << result << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [notes-rescue-orphans]\n";
        }
    }

    // [notes-inject-spans] — InjectNoteSpans replaces the HTML-escaped comment form
    // that ProcessInline produces (&lt;!-- note:N --&gt;) with a rendered span.
    {
        // Simulate what ProcessInline produces for inline <!-- note:N --> markers.
        std::string md = "<p>Hello world&lt;!-- note:3 --&gt; and&lt;!-- note:7 --&gt; done.</p>\n";
        std::map<int,std::string> texts;
        texts[3] = "Note three";
        texts[7] = "Note & \"seven\"";
        std::string result = InjectNoteSpans(md, texts);

        bool hasSpan3   = result.find("data-note-id=\"3\"") != std::string::npos;
        bool hasSpan7   = result.find("data-note-id=\"7\"") != std::string::npos;
        bool hasText3   = result.find("Note three") != std::string::npos;
        bool hasText7   = result.find("Note &amp; &quot;seven&quot;") != std::string::npos;
        bool noComments = result.find("&lt;!-- note:") == std::string::npos;
        bool hasClass   = result.find("note-marker") != std::string::npos;

        if (!hasSpan3 || !hasSpan7 || !hasText3 || !hasText7 || !noComments || !hasClass) {
            std::cerr << "FAIL [notes-inject-spans]: hasSpan3=" << hasSpan3
                      << " hasSpan7=" << hasSpan7
                      << " hasText3=" << hasText3
                      << " hasText7=" << hasText7
                      << " noComments=" << noComments
                      << " hasClass=" << hasClass
                      << "\n  result: '" << result << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [notes-inject-spans]\n";
        }
    }

    return failures;
}
