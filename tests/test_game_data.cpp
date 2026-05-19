#include "game_data.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int test_game_data() {
    int failures = 0;

    // Basic roundtrip
    {
        fs::path tmp = fs::temp_directory_path() / "tt-gamedata-test.dat";
        GameData in;
        in.question   = "What is the Rule of Five?";
        in.choiceA    = "Five special member functions.";
        in.choiceB    = "Five virtual functions.";
        in.correctIsA = true;

        bool wrote = WriteGameFile(tmp.string(), in);
        GameData out = ReadGameFile(tmp.string());
        fs::remove(tmp);

        bool ok = wrote
               && out.question   == in.question
               && out.choiceA    == in.choiceA
               && out.choiceB    == in.choiceB
               && out.correctIsA == in.correctIsA;
        if (!ok) {
            std::cerr << "FAIL [game-data-roundtrip]: wrote=" << wrote
                      << " q='" << out.question << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [game-data-roundtrip]\n";
        }
    }

    // Multiline question (newlines encoded/decoded)
    {
        fs::path tmp = fs::temp_directory_path() / "tt-gamedata-nl.dat";
        GameData in;
        in.question   = "Line one.\nLine two.\nLine three.";
        in.choiceA    = "Correct.";
        in.choiceB    = "Wrong.";
        in.correctIsA = false;

        WriteGameFile(tmp.string(), in);
        GameData out = ReadGameFile(tmp.string());
        fs::remove(tmp);

        bool ok = out.question == in.question && !out.correctIsA;
        if (!ok) {
            std::cerr << "FAIL [game-data-newlines]: q='" << out.question << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [game-data-newlines]\n";
        }
    }

    // Missing file → empty GameData (no crash)
    {
        GameData out = ReadGameFile("/nonexistent/path/tt-game.dat");
        bool ok = out.question.empty() && out.choiceA.empty();
        if (!ok) {
            std::cerr << "FAIL [game-data-missing-file]\n";
            ++failures;
        } else {
            std::cout << "PASS [game-data-missing-file]\n";
        }
    }

    // WriteGameFiles / ReadGameFiles roundtrip (multi-question)
    {
        fs::path tmp = fs::temp_directory_path() / "tt-gamedata-multi.dat";
        std::vector<GameData> in(2);
        in[0].question = "Q1"; in[0].choiceA = "A1"; in[0].choiceB = "B1"; in[0].correctIsA = true;
        in[1].question = "Q2"; in[1].choiceA = "A2"; in[1].choiceB = "B2"; in[1].correctIsA = false;

        bool wrote = WriteGameFiles(tmp.string(), in);
        auto out   = ReadGameFiles(tmp.string());
        fs::remove(tmp);

        bool ok = wrote && out.size() == 2
               && out[0].question == "Q1" && out[0].correctIsA == true
               && out[1].question == "Q2" && out[1].correctIsA == false;
        if (!ok) {
            std::cerr << "FAIL [game-data-multi-roundtrip]: size=" << out.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [game-data-multi-roundtrip]\n";
        }
    }

    // ReadGameFiles on missing file → empty vector (no crash)
    {
        auto out = ReadGameFiles("/nonexistent/path/tt-multi.dat");
        bool ok  = out.empty();
        if (!ok) {
            std::cerr << "FAIL [game-data-multi-missing]: size=" << out.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [game-data-multi-missing]\n";
        }
    }

    // AppendGameFiles adds items to an existing file
    {
        fs::path tmp = fs::temp_directory_path() / "tt-gamedata-append.dat";
        std::vector<GameData> first(2);
        first[0].question = "Q1"; first[0].choiceA = "A1"; first[0].choiceB = "B1"; first[0].correctIsA = true;
        first[1].question = "Q2"; first[1].choiceA = "A2"; first[1].choiceB = "B2"; first[1].correctIsA = false;
        WriteGameFiles(tmp.string(), first);

        std::vector<GameData> more(2);
        more[0].question = "Q3"; more[0].choiceA = "A3"; more[0].choiceB = "B3"; more[0].correctIsA = true;
        more[1].question = "Q4"; more[1].choiceA = "A4"; more[1].choiceB = "B4"; more[1].correctIsA = false;
        bool appended = AppendGameFiles(tmp.string(), more);

        auto out = ReadGameFiles(tmp.string());
        fs::remove(tmp);

        bool ok = appended && out.size() == 4
               && out[2].question == "Q3"
               && out[3].question == "Q4";
        if (!ok) {
            std::cerr << "FAIL [game-data-append]: appended=" << appended
                      << " size=" << out.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [game-data-append]\n";
        }
    }

    return failures;
}
