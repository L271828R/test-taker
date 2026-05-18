#include "embeddings.h"
#include "corpus.h"
#include "exam_prompt.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int test_corpus() {
    int failures = 0;

    // ExamConfig.useCorpus defaults to false (corpus is opt-in per session)
    {
        ExamConfig cfg;
        if (cfg.useCorpus) {
            std::cerr << "FAIL [exam-cfg-use-corpus-default]: expected false\n";
            ++failures;
        } else {
            std::cout << "PASS [exam-cfg-use-corpus-default]\n";
        }
    }

    // ── ChunkText tests ──────────────────────────────────────────────────────

    // Empty input → empty result
    {
        auto chunks = ChunkText("", 350, 50);
        if (!chunks.empty()) {
            std::cerr << "FAIL [chunk-empty]: expected 0 chunks, got " << chunks.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [chunk-empty]\n";
        }
    }

    // Whitespace-only input → empty result
    {
        auto chunks = ChunkText("   \t\n  ", 350, 50);
        if (!chunks.empty()) {
            std::cerr << "FAIL [chunk-whitespace]: expected 0 chunks, got " << chunks.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [chunk-whitespace]\n";
        }
    }

    // Short text (< windowWords) → single chunk preserving the original text
    {
        auto chunks = ChunkText("hello world foo bar", 350, 50);
        if (chunks.size() != 1 || chunks[0] != "hello world foo bar") {
            std::cerr << "FAIL [chunk-short]: size=" << chunks.size()
                      << " text='" << (chunks.empty() ? "" : chunks[0]) << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [chunk-short]\n";
        }
    }

    // Long text → multiple chunks
    {
        std::string words;
        for (int i = 0; i < 800; ++i) {
            if (i) words += ' ';
            words += "word" + std::to_string(i);
        }
        auto chunks = ChunkText(words, 350, 50);
        if (chunks.size() < 2) {
            std::cerr << "FAIL [chunk-long]: expected >1 chunk, got " << chunks.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [chunk-long]\n";
        }
    }

    // Overlap: last word of chunk N appears at the start of chunk N+1
    {
        std::string words;
        for (int i = 0; i < 500; ++i) {
            if (i) words += ' ';
            words += "w" + std::to_string(i);
        }
        auto chunks = ChunkText(words, 200, 30);
        bool ok = false;
        if (chunks.size() >= 2) {
            const std::string& c0 = chunks[0];
            const std::string& c1 = chunks[1];
            auto last_sp = c0.rfind(' ');
            std::string last_word = (last_sp == std::string::npos) ? c0 : c0.substr(last_sp + 1);
            ok = c1.find(last_word) != std::string::npos;
        }
        if (!ok) {
            std::cerr << "FAIL [chunk-overlap]: overlap word not found in next chunk\n";
            ++failures;
        } else {
            std::cout << "PASS [chunk-overlap]\n";
        }
    }

    // ── Corpus CRUD tests ────────────────────────────────────────────────────

    // Open creates schema (tables documents + chunks must exist)
    {
        auto dir = fs::temp_directory_path() / "tt_corpus_schema";
        fs::create_directories(dir);
        std::string err;
        Corpus c((dir / "corpus.db").string());
        bool ok = c.Open(err) && c.IsOpen();
        if (!ok) {
            std::cerr << "FAIL [corpus-open]: " << err << "\n";
            ++failures;
        } else {
            std::cout << "PASS [corpus-open]\n";
        }
        fs::remove_all(dir);
    }

    // AddDocument + ListDocuments
    {
        auto dir = fs::temp_directory_path() / "tt_corpus_docs";
        fs::create_directories(dir);
        std::string err;
        Corpus c((dir / "corpus.db").string());
        c.Open(err);

        int id1 = c.AddDocument("doc1.txt", "/path/doc1.txt", err);
        int id2 = c.AddDocument("doc2.pdf", "/path/doc2.pdf", err);
        auto docs = c.ListDocuments();

        bool ok = id1 > 0 && id2 > 0
               && docs.size() == 2
               && docs[0].name == "doc1.txt"
               && docs[1].name == "doc2.pdf"
               && !docs[0].addedAt.empty();
        if (!ok) {
            std::cerr << "FAIL [corpus-add-list]: id1=" << id1 << " id2=" << id2
                      << " size=" << docs.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [corpus-add-list]\n";
        }
        fs::remove_all(dir);
    }

    // DeleteDocument removes the record
    {
        auto dir = fs::temp_directory_path() / "tt_corpus_del";
        fs::create_directories(dir);
        std::string err;
        Corpus c((dir / "corpus.db").string());
        c.Open(err);

        int id = c.AddDocument("gone.txt", "/gone.txt", err);
        c.DeleteDocument(id, err);
        auto docs = c.ListDocuments();

        if (!docs.empty()) {
            std::cerr << "FAIL [corpus-delete]: " << docs.size() << " docs remain\n";
            ++failures;
        } else {
            std::cout << "PASS [corpus-delete]\n";
        }
        fs::remove_all(dir);
    }

    // AddChunk + ChunkCount
    {
        auto dir = fs::temp_directory_path() / "tt_corpus_chunk";
        fs::create_directories(dir);
        std::string err;
        Corpus c((dir / "corpus.db").string());
        c.Open(err);

        int docId = c.AddDocument("chunks.txt", "/chunks.txt", err);
        std::vector<float> v = {1.0f, 0.0f, 0.0f};
        c.AddChunk(docId, 0, "first",  v, err);
        c.AddChunk(docId, 1, "second", v, err);

        int n = c.ChunkCount(docId);
        if (n != 2) {
            std::cerr << "FAIL [corpus-chunk-count]: expected 2, got " << n << "\n";
            ++failures;
        } else {
            std::cout << "PASS [corpus-chunk-count]\n";
        }
        fs::remove_all(dir);
    }

    // DeleteDocument cascades to chunks
    {
        auto dir = fs::temp_directory_path() / "tt_corpus_cascade";
        fs::create_directories(dir);
        std::string err;
        Corpus c((dir / "corpus.db").string());
        c.Open(err);

        int docId = c.AddDocument("cascade.txt", "/cascade.txt", err);
        std::vector<float> v = {1.0f};
        c.AddChunk(docId, 0, "a", v, err);
        c.AddChunk(docId, 1, "b", v, err);
        c.DeleteDocument(docId, err);

        int n = c.ChunkCount(docId);
        if (n != 0) {
            std::cerr << "FAIL [corpus-cascade]: " << n << " orphan chunks remain\n";
            ++failures;
        } else {
            std::cout << "PASS [corpus-cascade]\n";
        }
        fs::remove_all(dir);
    }

    // Search returns chunks ranked by cosine similarity (dot product on L2-norm vectors)
    {
        auto dir = fs::temp_directory_path() / "tt_corpus_knn";
        fs::create_directories(dir);
        std::string err;
        Corpus c((dir / "corpus.db").string());
        c.Open(err);

        int docId = c.AddDocument("vecs.txt", "/vecs.txt", err);

        // 4-dim unit vectors
        std::vector<float> v0 = {1.0f, 0.0f, 0.0f, 0.0f};          // cos(q,v0)=1.0
        std::vector<float> v1 = {0.0f, 1.0f, 0.0f, 0.0f};          // cos(q,v1)=0.0
        std::vector<float> v2 = {0.707f, 0.707f, 0.0f, 0.0f};      // cos(q,v2)≈0.707

        c.AddChunk(docId, 0, "chunk zero", v0, err);
        c.AddChunk(docId, 1, "chunk one",  v1, err);
        c.AddChunk(docId, 2, "chunk two",  v2, err);

        auto results = c.Search(v0, 3);
        bool ok = results.size() == 3
               && results[0].text == "chunk zero"
               && results[1].text == "chunk two";
        if (!ok) {
            std::cerr << "FAIL [corpus-knn]: size=" << results.size()
                      << " top='" << (results.empty() ? "none" : results[0].text) << "'"
                      << " second='" << (results.size() < 2 ? "none" : results[1].text) << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [corpus-knn]\n";
        }
        fs::remove_all(dir);
    }

    // Search respects topK limit
    {
        auto dir = fs::temp_directory_path() / "tt_corpus_topk";
        fs::create_directories(dir);
        std::string err;
        Corpus c((dir / "corpus.db").string());
        c.Open(err);

        int docId = c.AddDocument("topk.txt", "/topk.txt", err);
        std::vector<float> v = {1.0f};
        for (int i = 0; i < 10; ++i)
            c.AddChunk(docId, i, "chunk " + std::to_string(i), v, err);

        auto results = c.Search(v, 3);
        if (results.size() != 3) {
            std::cerr << "FAIL [corpus-topk]: expected 3, got " << results.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [corpus-topk]\n";
        }
        fs::remove_all(dir);
    }

    // CopyFileToCorpusDir copies file into <projectDir>/corpus/<filename>
    {
        auto projDir = fs::temp_directory_path() / "tt_copy_test";
        fs::remove_all(projDir);
        fs::create_directories(projDir);

        fs::path src = fs::temp_directory_path() / "tt_copy_src.txt";
        { std::ofstream f(src); f << "hello world"; }

        std::string err;
        std::string dest = CopyFileToCorpusDir(projDir.string(), src.string(), err);

        fs::path expected = projDir / "corpus" / "tt_copy_src.txt";
        bool ok = err.empty()
               && !dest.empty()
               && fs::exists(dest)
               && dest == expected.string();
        if (!ok) {
            std::cerr << "FAIL [corpus-copy-to-dir]: err='" << err
                      << "' dest='" << dest << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [corpus-copy-to-dir]\n";
        }
        fs::remove_all(projDir);
        fs::remove(src);
    }

    return failures;
}
