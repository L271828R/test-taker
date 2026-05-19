#include "corpus.h"
#include "embeddings.h"
#include "rag_logger.h"
#include <sqlite3.h>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

std::string CorpusDocGroup(const std::string& docPath, const std::string& projectDir) {
    std::string prefix = projectDir + "/corpus/";
    if (docPath.rfind(prefix, 0) != 0) return {};
    std::string rel = docPath.substr(prefix.size());
    size_t slash = rel.find('/');
    if (slash == std::string::npos) return {};
    return rel.substr(0, slash);
}

std::string CorpusContextFor(const std::string& projectDir,
                              const std::string& query,
                              const std::string& ollamaUrl,
                              const std::string& logContext,
                              int                topK) {
    std::string dbPath = projectDir + "/corpus.db";
    if (!fs::exists(dbPath)) return {};

    Corpus corpus(dbPath);
    std::string err;
    if (!corpus.Open(err)) return {};

    auto emb = EmbedText(query, ollamaUrl);
    if (!emb.ok) return {};

    auto hits = corpus.Search(emb.embedding, topK);
    if (hits.empty()) return {};

    // Build context string and log to RAG log.
    std::string ctx = "Relevant excerpts from the study corpus:\n\n";
    std::vector<RagChunk> ragChunks;
    for (const auto& h : hits) {
        ctx += h.text + "\n\n---\n\n";
        ragChunks.push_back({h.score, h.docName, h.text});
    }
    RagLogger::get().logEvent(logContext.empty() ? "Unknown" : logContext, query, ragChunks);
    return ctx;
}

std::string CopyFileToCorpusDir(const std::string& projectDir,
                                 const std::string& srcPath,
                                 std::string& err) {
    fs::path destDir = fs::path(projectDir) / "corpus";
    std::error_code ec;
    fs::create_directories(destDir, ec);
    if (ec) { err = ec.message(); return {}; }

    fs::path dest = destDir / fs::path(srcPath).filename();
    fs::copy_file(srcPath, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) { err = ec.message(); return {}; }

    return dest.string();
}

static sqlite3* asDb(void* p) { return static_cast<sqlite3*>(p); }

static std::string iso_now() {
    std::time_t t = std::time(nullptr);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
    return buf;
}

Corpus::Corpus(const std::string& dbPath) : m_dbPath(dbPath) {}

Corpus::~Corpus() {
    if (m_db) sqlite3_close(asDb(m_db));
}

bool Corpus::Open(std::string& err) {
    sqlite3* db = nullptr;
    if (sqlite3_open(m_dbPath.c_str(), &db) != SQLITE_OK) {
        err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }
    m_db = db;
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;",  nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;",   nullptr, nullptr, nullptr);
    return EnsureSchema(err);
}

bool Corpus::IsOpen() const { return m_db != nullptr; }

bool Corpus::EnsureSchema(std::string& err) {
    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS documents (
            id       INTEGER PRIMARY KEY AUTOINCREMENT,
            name     TEXT NOT NULL,
            path     TEXT NOT NULL,
            added_at TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS chunks (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
            chunk_index INTEGER NOT NULL,
            text        TEXT NOT NULL,
            embedding   BLOB NOT NULL
        );
    )SQL";
    char* dbErr = nullptr;
    if (sqlite3_exec(asDb(m_db), sql, nullptr, nullptr, &dbErr) != SQLITE_OK) {
        err = dbErr ? dbErr : "schema error";
        sqlite3_free(dbErr);
        return false;
    }
    return true;
}

int Corpus::AddDocument(const std::string& name, const std::string& path, std::string& err) {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(asDb(m_db),
        "INSERT INTO documents (name,path,added_at) VALUES (?,?,?)", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, path.c_str(), -1, SQLITE_TRANSIENT);
    std::string now = iso_now();
    sqlite3_bind_text(s, 3, now.c_str(),  -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    if (rc != SQLITE_DONE) { err = sqlite3_errmsg(asDb(m_db)); return -1; }
    return static_cast<int>(sqlite3_last_insert_rowid(asDb(m_db)));
}

bool Corpus::DeleteDocument(int id, std::string& err) {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(asDb(m_db), "DELETE FROM documents WHERE id=?", -1, &s, nullptr);
    sqlite3_bind_int(s, 1, id);
    int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    if (rc != SQLITE_DONE) { err = sqlite3_errmsg(asDb(m_db)); return false; }
    return true;
}

std::vector<CorpusDoc> Corpus::ListDocuments() const {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(asDb(m_db),
        "SELECT id,name,path,added_at FROM documents ORDER BY id", -1, &s, nullptr);
    std::vector<CorpusDoc> docs;
    while (sqlite3_step(s) == SQLITE_ROW) {
        CorpusDoc d;
        d.id      = sqlite3_column_int(s, 0);
        d.name    = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
        d.path    = reinterpret_cast<const char*>(sqlite3_column_text(s, 2));
        d.addedAt = reinterpret_cast<const char*>(sqlite3_column_text(s, 3));
        docs.push_back(std::move(d));
    }
    sqlite3_finalize(s);
    return docs;
}

bool Corpus::AddChunk(int documentId, int chunkIndex,
                       const std::string& text, const std::vector<float>& embedding,
                       std::string& err) {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(asDb(m_db),
        "INSERT INTO chunks (document_id,chunk_index,text,embedding) VALUES (?,?,?,?)",
        -1, &s, nullptr);
    sqlite3_bind_int(s,  1, documentId);
    sqlite3_bind_int(s,  2, chunkIndex);
    sqlite3_bind_text(s, 3, text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(s, 4, embedding.data(),
                      static_cast<int>(embedding.size() * sizeof(float)), SQLITE_TRANSIENT);
    int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    if (rc != SQLITE_DONE) { err = sqlite3_errmsg(asDb(m_db)); return false; }
    return true;
}

void Corpus::DeleteChunksForDocument(int documentId) {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(asDb(m_db), "DELETE FROM chunks WHERE document_id=?", -1, &s, nullptr);
    sqlite3_bind_int(s, 1, documentId);
    sqlite3_step(s);
    sqlite3_finalize(s);
}

int Corpus::ChunkCount(int documentId) const {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(asDb(m_db),
        "SELECT COUNT(*) FROM chunks WHERE document_id=?", -1, &s, nullptr);
    sqlite3_bind_int(s, 1, documentId);
    int n = 0;
    if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    return n;
}

std::vector<CorpusSearchResult> Corpus::Search(const std::vector<float>& query, int topK) const {
    struct Row {
        int chunkId, documentId;
        std::string docName, text;
        std::vector<float> vec;
        float score = 0.0f;
    };

    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(asDb(m_db),
        "SELECT c.id,c.document_id,d.name,c.text,c.embedding "
        "FROM chunks c JOIN documents d ON d.id=c.document_id",
        -1, &s, nullptr);

    std::vector<Row> rows;
    while (sqlite3_step(s) == SQLITE_ROW) {
        Row r;
        r.chunkId    = sqlite3_column_int(s, 0);
        r.documentId = sqlite3_column_int(s, 1);
        r.docName    = reinterpret_cast<const char*>(sqlite3_column_text(s, 2));
        r.text       = reinterpret_cast<const char*>(sqlite3_column_text(s, 3));
        const void* blob  = sqlite3_column_blob(s, 4);
        int         bytes = sqlite3_column_bytes(s, 4);
        r.vec.resize(static_cast<size_t>(bytes) / sizeof(float));
        std::memcpy(r.vec.data(), blob, static_cast<size_t>(bytes));
        rows.push_back(std::move(r));
    }
    sqlite3_finalize(s);

    int qdim = static_cast<int>(query.size());
    for (auto& row : rows) {
        float dot = 0.0f;
        int   dim = std::min(qdim, static_cast<int>(row.vec.size()));
        for (int i = 0; i < dim; ++i) dot += query[i] * row.vec[i];
        row.score = dot;
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b){ return a.score > b.score; });

    std::vector<CorpusSearchResult> out;
    int limit = std::min(topK, static_cast<int>(rows.size()));
    out.reserve(static_cast<size_t>(limit));
    for (int i = 0; i < limit; ++i)
        out.push_back({rows[i].chunkId, rows[i].documentId,
                       rows[i].docName, rows[i].text, rows[i].score});
    return out;
}
