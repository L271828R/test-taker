// tests/main.cpp
// Run with: cmake --build build --target test_test-taker && ./build/test_test-taker

#include <iostream>

int test_config();
int test_markdown();
int test_html_template();
int test_mdviewer();
int test_project();
int test_creator();
int test_editor();
int test_git_ops();
int test_llm_error();
int test_llm_response();
int test_conversation();
int test_meta();
int test_project_search();
int test_notes();
int test_session();
int test_exam_meta();
int test_exam_prompt();
int test_turn_chat();
int test_corpus();
int test_web_fetch();
int test_git_import();
int test_saved_convos();
int test_game_data();

int main() {
    int failures = 0;
    failures += test_config();
    failures += test_markdown();
    failures += test_html_template();
    failures += test_mdviewer();
    failures += test_project();
    failures += test_creator();
    failures += test_editor();
    failures += test_git_ops();
    failures += test_llm_error();
    failures += test_llm_response();
    failures += test_conversation();
    failures += test_meta();
    failures += test_project_search();
    failures += test_notes();
    failures += test_session();
    failures += test_exam_meta();
    failures += test_exam_prompt();
    failures += test_turn_chat();
    failures += test_corpus();
    failures += test_web_fetch();
    failures += test_git_import();
    failures += test_saved_convos();
    failures += test_game_data();
    std::cout << (failures == 0 ? "ALL PASSED" : "FAILED") << "\n";
    return failures > 0 ? 1 : 0;
}
