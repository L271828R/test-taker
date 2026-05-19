cmake_minimum_required(VERSION 3.16)
project(test-taker CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(wxWidgets REQUIRED COMPONENTS core base webview)
include(${wxWidgets_USE_FILE})

# ── Mermaid.js — downloaded ONCE at configure time, then bundled offline ─────
set(MERMAID_JS "${CMAKE_CURRENT_SOURCE_DIR}/mermaid.min.js")
if(NOT EXISTS "${MERMAID_JS}")
    message(STATUS "Downloading mermaid.min.js (one-time only — bundled into binary for offline use)…")
    file(DOWNLOAD
        "https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.min.js"
        "${MERMAID_JS}"
        SHOW_PROGRESS
        STATUS MERMAID_DL
    )
    list(GET MERMAID_DL 0 MERMAID_DL_CODE)
    if(NOT MERMAID_DL_CODE EQUAL 0)
        message(FATAL_ERROR
            "Failed to download mermaid.min.js (error ${MERMAID_DL_CODE}).\n"
            "Download manually and place at:\n  ${MERMAID_JS}\n"
            "  curl -L https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.min.js"
            " -o \"${MERMAID_JS}\"")
    endif()
endif()

# Embed mermaid.min.js as a C++ byte array using xxd
find_program(XXD xxd REQUIRED)
set(MERMAID_JS_H "${CMAKE_CURRENT_BINARY_DIR}/mermaid_js.h")
add_custom_command(
    OUTPUT  "${MERMAID_JS_H}"
    COMMAND ${XXD} -i -n mermaid_js_data "${MERMAID_JS}" > "${MERMAID_JS_H}"
    DEPENDS "${MERMAID_JS}"
    COMMENT "Embedding mermaid.min.js into binary"
    VERBATIM
)
add_custom_target(gen_mermaid_header ALL DEPENDS "${MERMAID_JS_H}")
# ─────────────────────────────────────────────────────────────────────────────

# ── Highlight.js — downloaded ONCE at configure time, bundled offline ─────────
set(HLJS_JS        "${CMAKE_CURRENT_SOURCE_DIR}/highlight.min.js")
set(HLJS_CSS_LIGHT "${CMAKE_CURRENT_SOURCE_DIR}/highlight-light.min.css")
set(HLJS_CSS_DARK  "${CMAKE_CURRENT_SOURCE_DIR}/highlight-dark.min.css")

foreach(_PAIR IN ITEMS
    "https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js||${HLJS_JS}||highlight.min.js"
    "https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/github.min.css||${HLJS_CSS_LIGHT}||highlight-light.min.css"
    "https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/github-dark.min.css||${HLJS_CSS_DARK}||highlight-dark.min.css"
)
    string(REPLACE "||" ";" _P "${_PAIR}")
    list(GET _P 0 _URL)
    list(GET _P 1 _DEST)
    list(GET _P 2 _LABEL)
    if(NOT EXISTS "${_DEST}")
        message(STATUS "Downloading ${_LABEL} (one-time only — bundled into binary for offline use)…")
        file(DOWNLOAD "${_URL}" "${_DEST}" SHOW_PROGRESS STATUS _DL)
        list(GET _DL 0 _DL_CODE)
        if(NOT _DL_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download ${_LABEL} (error ${_DL_CODE}).")
        endif()
    endif()
endforeach()

set(HLJS_JS_H        "${CMAKE_CURRENT_BINARY_DIR}/hljs_js.h")
set(HLJS_CSS_LIGHT_H "${CMAKE_CURRENT_BINARY_DIR}/hljs_css_light.h")
set(HLJS_CSS_DARK_H  "${CMAKE_CURRENT_BINARY_DIR}/hljs_css_dark.h")

add_custom_command(OUTPUT "${HLJS_JS_H}"
    COMMAND ${XXD} -i -n hljs_js_data "${HLJS_JS}" > "${HLJS_JS_H}"
    DEPENDS "${HLJS_JS}" COMMENT "Embedding highlight.min.js" VERBATIM)
add_custom_command(OUTPUT "${HLJS_CSS_LIGHT_H}"
    COMMAND ${XXD} -i -n hljs_css_light_data "${HLJS_CSS_LIGHT}" > "${HLJS_CSS_LIGHT_H}"
    DEPENDS "${HLJS_CSS_LIGHT}" COMMENT "Embedding highlight-light.min.css" VERBATIM)
add_custom_command(OUTPUT "${HLJS_CSS_DARK_H}"
    COMMAND ${XXD} -i -n hljs_css_dark_data "${HLJS_CSS_DARK}" > "${HLJS_CSS_DARK_H}"
    DEPENDS "${HLJS_CSS_DARK}" COMMENT "Embedding highlight-dark.min.css" VERBATIM)

add_custom_target(gen_hljs_headers ALL
    DEPENDS "${HLJS_JS_H}" "${HLJS_CSS_LIGHT_H}" "${HLJS_CSS_DARK_H}")
# ─────────────────────────────────────────────────────────────────────────────

# ── SQLite3 ──────────────────────────────────────────────────────────────────
# Prefer Homebrew's sqlite on macOS (system SDK doesn't expose headers cleanly).
if(APPLE)
    execute_process(COMMAND brew --prefix sqlite
        OUTPUT_VARIABLE _BREW_SQLITE
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_BREW_SQLITE AND EXISTS "${_BREW_SQLITE}/include/sqlite3.h")
        set(SQLite3_INCLUDE_DIRS "${_BREW_SQLITE}/include")
        set(SQLite3_LIBRARIES    "${_BREW_SQLITE}/lib/libsqlite3.dylib")
        message(STATUS "SQLite3: Homebrew at ${_BREW_SQLITE}")
    endif()
endif()
if(NOT SQLite3_LIBRARIES)
    find_package(SQLite3 REQUIRED)
endif()
# ─────────────────────────────────────────────────────────────────────────────

set(SOURCES
    src/main.cpp
    src/app_frame.cpp
    src/new_session_panel.cpp
    src/exam_panel.cpp
    src/turn_chat_panel.cpp
    src/review_panel.cpp
    src/chat_panel.cpp
    src/project_panel.cpp
    src/llm.cpp
    src/markdown.cpp
    src/html_template.cpp
    src/config.cpp
    src/project.cpp
    src/project_search.cpp
    src/conversation.cpp
    src/git_ops.cpp
    src/llm_error.cpp
    src/llm_response.cpp
    src/logger.cpp
    src/meta.cpp
    src/session.cpp
    src/exam_meta.cpp
    src/exam_prompt.cpp
    src/turn_chat.cpp
    src/inspector.mm
    src/corpus.cpp
    src/embeddings.cpp
    src/corpus_panel.cpp
    src/web_fetch.cpp
    src/git_import.cpp
    src/rag_logger.cpp
    src/focus_list_panel.cpp
    src/personality_panel.cpp
    src/personality_lib.cpp
    src/saved_convos.cpp
    src/saved_panel.cpp
    src/game_data.cpp
)
add_executable(test-taker ${SOURCES})
add_dependencies(test-taker gen_mermaid_header gen_hljs_headers)

target_include_directories(test-taker PRIVATE src ${CMAKE_CURRENT_BINARY_DIR} ${SQLite3_INCLUDE_DIRS})
target_link_libraries(test-taker PRIVATE ${wxWidgets_LIBRARIES} ${SQLite3_LIBRARIES})

# ── Tests (no wxWidgets dependency) ──────────────────────────────────────────
add_executable(test_test-taker
    tests/main.cpp
    tests/test_config.cpp
    tests/test_markdown.cpp
    tests/test_html_template.cpp
    tests/test_mdviewer.cpp
    tests/test_project.cpp
    tests/test_creator.cpp
    tests/test_editor.cpp
    tests/test_git_ops.cpp
    tests/test_llm_error.cpp
    tests/test_llm_response.cpp
    tests/test_conversation.cpp
    tests/test_meta.cpp
    tests/test_project_search.cpp
    tests/test_notes.cpp
    tests/test_session.cpp
    tests/test_exam_meta.cpp
    tests/test_exam_prompt.cpp
    tests/test_turn_chat.cpp
    tests/test_corpus.cpp
    tests/test_web_fetch.cpp
    tests/test_git_import.cpp
    tests/test_saved_convos.cpp
    tests/test_game_data.cpp
    src/config.cpp
    src/markdown.cpp
    src/html_template.cpp
    src/project.cpp
    src/project_search.cpp
    src/creator.cpp
    src/conversation.cpp
    src/editor.cpp
    src/notes.cpp
    src/git_ops.cpp
    src/llm_error.cpp
    src/llm_response.cpp
    src/meta.cpp
    src/session.cpp
    src/exam_meta.cpp
    src/exam_prompt.cpp
    src/turn_chat.cpp
    src/corpus.cpp
    src/embeddings.cpp
    src/web_fetch.cpp
    src/git_import.cpp
    src/rag_logger.cpp
    src/saved_convos.cpp
    src/personality_lib.cpp
    src/game_data.cpp
)
add_dependencies(test_test-taker gen_mermaid_header gen_hljs_headers)
target_include_directories(test_test-taker PRIVATE src ${CMAKE_CURRENT_BINARY_DIR} ${SQLite3_INCLUDE_DIRS})
target_link_libraries(test_test-taker PRIVATE ${SQLite3_LIBRARIES})

enable_testing()
add_test(NAME dark_mode COMMAND test_test-taker)

if(APPLE)
    find_library(WEBKIT_FW WebKit)
    if(WEBKIT_FW)
        target_link_libraries(test-taker PRIVATE ${WEBKIT_FW})
    endif()
endif()

install(TARGETS test-taker DESTINATION bin)

# ── SDL2 quiz game subprocess ─────────────────────────────────────────────────
find_package(SDL2  REQUIRED CONFIG HINTS /opt/homebrew/lib/cmake/SDL2)
find_package(SDL2_ttf REQUIRED CONFIG HINTS /opt/homebrew/lib/cmake/SDL2_ttf)

add_executable(test-taker-game game/main.cpp src/game_data.cpp)
target_include_directories(test-taker-game PRIVATE src /opt/homebrew/include)
target_link_libraries(test-taker-game PRIVATE SDL2::SDL2 SDL2_ttf::SDL2_ttf)
set_target_properties(test-taker-game PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
