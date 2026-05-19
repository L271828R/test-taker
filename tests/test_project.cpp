#include "project.h"
#include "personality_lib.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static fs::path make_temp_dir() {
    auto ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto p = fs::temp_directory_path() / ("mdviewer_test_" + std::to_string(ns));
    fs::create_directories(p);
    return p;
}

int test_project() {
    int failures = 0;

    // CreateProject creates the folder, a claude.md stub, and an empty .index.
    {
        auto base = make_temp_dir();
        bool ok       = CreateProject(base.string(), "my-story");
        fs::path proj = base / "my-story";
        bool dirExists = fs::exists(proj);
        bool claudeMd  = fs::exists(proj / "claude.md");
        bool index     = fs::exists(proj / ".index");
        if (!ok || !dirExists || !claudeMd || !index) {
            std::cerr << "FAIL [create-project]: ok=" << ok
                      << " dir=" << dirExists
                      << " claude.md=" << claudeMd
                      << " .index=" << index << "\n";
            ++failures;
        } else {
            std::cout << "PASS [create-project]\n";
        }
        fs::remove_all(base);
    }

    // ProjectExists returns true for a valid project, false for an absent one.
    {
        auto base = make_temp_dir();
        CreateProject(base.string(), "p1");
        bool yes = ProjectExists((base / "p1").string());
        bool no  = ProjectExists((base / "nope").string());
        if (!yes || no) {
            std::cerr << "FAIL [project-exists]: yes=" << yes << " no=" << no << "\n";
            ++failures;
        } else {
            std::cout << "PASS [project-exists]\n";
        }
        fs::remove_all(base);
    }

    // SaveConfig / LoadConfig round-trips all fields.
    {
        auto base = make_temp_dir();
        CreateProject(base.string(), "cfg-test");
        std::string proj = (base / "cfg-test").string();

        ProjectConfig cfg;
        cfg.llmBackend  = "ollama";
        cfg.ollamaModel = "llama3";
        cfg.ollamaUrl   = "http://localhost:11434";
        cfg.apiKey      = "sk-test";
        SaveConfig(proj, cfg);

        ProjectConfig got = LoadConfig(proj);
        if (got.llmBackend  != "ollama" ||
            got.ollamaModel != "llama3" ||
            got.ollamaUrl   != "http://localhost:11434" ||
            got.apiKey      != "sk-test") {
            std::cerr << "FAIL [config-roundtrip]: backend=" << got.llmBackend
                      << " model=" << got.ollamaModel
                      << " url=" << got.ollamaUrl
                      << " key=" << got.apiKey << "\n";
            ++failures;
        } else {
            std::cout << "PASS [config-roundtrip]\n";
        }
        fs::remove_all(base);
    }

    // RegisterChapter assigns monotonically increasing IDs; ChapterFile retrieves them.
    {
        auto base = make_temp_dir();
        CreateProject(base.string(), "ch-test");
        std::string proj = (base / "ch-test").string();

        int id1 = RegisterChapter(proj, "chapter_01.md");
        int id2 = RegisterChapter(proj, "chapter_02.md");
        std::string f1 = ChapterFile(proj, id1);
        std::string f2 = ChapterFile(proj, id2);
        if (id2 <= id1 || f1 != "chapter_01.md" || f2 != "chapter_02.md") {
            std::cerr << "FAIL [chapter-register]: id1=" << id1 << " id2=" << id2
                      << " f1=" << f1 << " f2=" << f2 << "\n";
            ++failures;
        } else {
            std::cout << "PASS [chapter-register]\n";
        }
        fs::remove_all(base);
    }

    // RegisterTidbit assigns monotonically increasing IDs; TidbitLocation retrieves them.
    {
        auto base = make_temp_dir();
        CreateProject(base.string(), "tb-test");
        std::string proj = (base / "tb-test").string();

        int chId = RegisterChapter(proj, "ch1.md");
        int tb1  = RegisterTidbit(proj, chId, 0);
        int tb2  = RegisterTidbit(proj, chId, 1);
        auto [ch1, pos1] = TidbitLocation(proj, tb1);
        auto [ch2, pos2] = TidbitLocation(proj, tb2);
        if (tb2 <= tb1 || ch1 != chId || pos1 != 0 || ch2 != chId || pos2 != 1) {
            std::cerr << "FAIL [tidbit-register]: tb1=" << tb1 << " tb2=" << tb2
                      << " ch1=" << ch1 << " pos1=" << pos1
                      << " ch2=" << ch2 << " pos2=" << pos2 << "\n";
            ++failures;
        } else {
            std::cout << "PASS [tidbit-register]\n";
        }
        fs::remove_all(base);
    }

    // Unknown chapter / tidbit IDs return sentinel values.
    {
        auto base = make_temp_dir();
        CreateProject(base.string(), "unk-test");
        std::string proj = (base / "unk-test").string();

        std::string f = ChapterFile(proj, 999);
        auto [ch, pos] = TidbitLocation(proj, 999);
        if (!f.empty() || ch != -1 || pos != -1) {
            std::cerr << "FAIL [unknown-id]: expected empty/negative for unknown ids; "
                      << "f='" << f << "' ch=" << ch << " pos=" << pos << "\n";
            ++failures;
        } else {
            std::cout << "PASS [unknown-id]\n";
        }
        fs::remove_all(base);
    }

    // IDs persist across separate LoadIndex calls (written to disk).
    {
        auto base = make_temp_dir();
        CreateProject(base.string(), "persist-test");
        std::string proj = (base / "persist-test").string();

        int id = RegisterChapter(proj, "ch.md");
        // Reload from disk by calling ChapterFile (which re-reads the index).
        std::string f = ChapterFile(proj, id);
        if (f != "ch.md") {
            std::cerr << "FAIL [persist]: got '" << f << "' after reload\n";
            ++failures;
        } else {
            std::cout << "PASS [persist]\n";
        }
        fs::remove_all(base);
    }

    // InitProject initialises an existing directory in place (no subdirectory).
    {
        auto base = make_temp_dir();
        bool ok       = InitProject(base.string());
        bool claudeMd = fs::exists(base / "claude.md");
        bool index    = fs::exists(base / ".index");
        bool isProj   = ProjectExists(base.string());
        if (!ok || !claudeMd || !index || !isProj) {
            std::cerr << "FAIL [init-project]: ok=" << ok
                      << " claude.md=" << claudeMd
                      << " .index=" << index
                      << " isProj=" << isProj << "\n";
            ++failures;
        } else {
            std::cout << "PASS [init-project]\n";
        }
        fs::remove_all(base);
    }

    // InitProject is idempotent — calling it twice does not reset the index.
    {
        auto base = make_temp_dir();
        InitProject(base.string());
        int id = RegisterChapter(base.string(), "ch1.md");
        InitProject(base.string());  // second call must not reset counter
        int id2 = RegisterChapter(base.string(), "ch2.md");
        if (id2 <= id) {
            std::cerr << "FAIL [init-idempotent]: id=" << id << " id2=" << id2 << "\n";
            ++failures;
        } else {
            std::cout << "PASS [init-idempotent]\n";
        }
        fs::remove_all(base);
    }

    // NextChapterId / NextTidbitId peek the next ID without consuming it.
    {
        auto base = make_temp_dir();
        CreateProject(base.string(), "peek-test");
        std::string proj = (base / "peek-test").string();

        int peek1 = NextChapterId(proj);
        int peek2 = NextChapterId(proj);  // calling twice must return same value
        int assigned = RegisterChapter(proj, "ch.md");
        int peek3 = NextChapterId(proj);  // now must be assigned+1

        bool sameBeforeUse = (peek1 == peek2);
        bool matchesAssigned = (peek1 == assigned);
        bool advancedAfter = (peek3 == assigned + 1);

        if (!sameBeforeUse || !matchesAssigned || !advancedAfter) {
            std::cerr << "FAIL [next-id-peek]: peek1=" << peek1 << " peek2=" << peek2
                      << " assigned=" << assigned << " peek3=" << peek3 << "\n";
            ++failures;
        } else {
            std::cout << "PASS [next-id-peek]\n";
        }
        fs::remove_all(base);
    }

    // CreateProject stub claude.md should describe an exam project, not a story.
    {
        auto base = make_temp_dir();
        CreateProject(base.string(), "spanish-vocab");
        std::string proj = (base / "spanish-vocab").string();

        std::ifstream f(proj + "/claude.md");
        std::string content((std::istreambuf_iterator<char>(f)), {});

        bool hasName     = content.find("spanish-vocab") != std::string::npos;
        bool hasExamWord = content.find("exam") != std::string::npos
                        || content.find("topic") != std::string::npos
                        || content.find("question") != std::string::npos;
        bool noStory     = content.find("generation prompt") == std::string::npos;

        if (!hasName || !hasExamWord || !noStory) {
            std::cerr << "FAIL [create-project-stub]: hasName=" << hasName
                      << " hasExamWord=" << hasExamWord
                      << " noStory=" << noStory << "\n"
                      << "content: " << content << "\n";
            ++failures;
        } else {
            std::cout << "PASS [create-project-stub]\n";
        }
        fs::remove_all(base);
    }

    // ProjectConfig personalities field roundtrips through SaveConfig/LoadConfig.
    {
        auto base = make_temp_dir();
        CreateProject(base.string(), "pers-test");
        std::string proj = (base / "pers-test").string();

        ProjectConfig cfg;
        cfg.name         = "pers-test";
        cfg.personalities = "Albert Einstein|Bill Gates|Linus Torvalds";
        bool saved = SaveConfig(proj, cfg);

        ProjectConfig loaded = LoadConfig(proj);
        bool ok = saved && loaded.personalities == "Albert Einstein|Bill Gates|Linus Torvalds";
        if (!ok) {
            std::cerr << "FAIL [project-config-personalities]: saved=" << saved
                      << " personalities='" << loaded.personalities << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [project-config-personalities]\n";
        }
        fs::remove_all(base);
    }

    // ProjectConfig exam form state roundtrips through SaveConfig/LoadConfig.
    {
        auto base = make_temp_dir();
        CreateProject(base.string(), "exam-state-test");
        std::string proj = (base / "exam-state-test").string();

        ProjectConfig cfg;
        cfg.name             = "exam-state-test";
        cfg.examTopic        = "AWS Security";
        cfg.examInstructions = "Focus on IAM policies";
        cfg.examFocusAreas   = "5@@IAM|3@@S3";
        cfg.examBackend      = "Anthropic API";
        cfg.examApiKey       = "sk-ant-test";
        cfg.examOllamaModel  = "llama3";
        cfg.lastSession      = "session_20260518_103000.md";
        SaveConfig(proj, cfg);

        ProjectConfig loaded = LoadConfig(proj);
        bool ok = loaded.examTopic        == "AWS Security"
               && loaded.examInstructions == "Focus on IAM policies"
               && loaded.examFocusAreas   == "5@@IAM|3@@S3"
               && loaded.examBackend      == "Anthropic API"
               && loaded.examApiKey       == "sk-ant-test"
               && loaded.examOllamaModel  == "llama3"
               && loaded.lastSession      == "session_20260518_103000.md";
        if (!ok) {
            std::cerr << "FAIL [project-config-exam-state]:"
                      << " topic='" << loaded.examTopic << "'"
                      << " lastSession='" << loaded.lastSession << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [project-config-exam-state]\n";
        }
        fs::remove_all(base);
    }

    // DefaultPersonalityLibrary has the expected four categories with characters.
    {
        auto lib = DefaultPersonalityLibrary();
        bool hasProg   = lib.count("Programming") && lib.at("Programming").size() >= 4;
        bool hasSci    = lib.count("Science")     && lib.at("Science").size()     >= 4;
        bool hasLit    = lib.count("Literature")  && lib.at("Literature").size()  >= 3;
        bool hasPhil   = lib.count("Philosophy")  && lib.at("Philosophy").size()  >= 3;
        bool hasGates  = hasProg && std::find(lib.at("Programming").begin(),
                                              lib.at("Programming").end(),
                                              "Bill Gates") != lib.at("Programming").end();
        bool hasEin    = hasSci  && std::find(lib.at("Science").begin(),
                                              lib.at("Science").end(),
                                              "Albert Einstein") != lib.at("Science").end();
        bool ok = hasProg && hasSci && hasLit && hasPhil && hasGates && hasEin;
        if (!ok) {
            std::cerr << "FAIL [default-personality-library]: "
                      << "prog=" << hasProg << " sci=" << hasSci
                      << " lit=" << hasLit  << " phil=" << hasPhil
                      << " gates=" << hasGates << " einstein=" << hasEin << "\n";
            ++failures;
        } else {
            std::cout << "PASS [default-personality-library]\n";
        }
    }

    return failures;
}
