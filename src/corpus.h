#pragma once
#include <string>
#include <vector>

struct CorpusDoc {
    int id = 0;
    std::string name;
    std::string path;
    std::string addedAt;
};

struct CorpusSearchResult {
    int chunkId    = 0;
    int documentId = 0;
    std::string docName;
    std::string text;
    float score = 0.0f;
};

// Copies srcPath into <projectDir>/corpus/<filename>, creating the directory if needed.
// Returns the destination path on success, or empty string + sets err on failure.
std::string CopyFileToCorpusDir(const std::string& projectDir,
                                 const std::string& srcPath,
                                 std::string& err);

class Corpus {
public:
    explicit Corpus(const std::string& dbPath);
    ~Corpus();

    bool Open(std::string& err);
    bool IsOpen() const;

    // Returns the new document id (> 0) on success, -1 on failure.
    int  AddDocument(const std::string& name, const std::string& path, std::string& err);
    bool DeleteDocument(int id, std::string& err);
    std::vector<CorpusDoc> ListDocuments() const;

    bool AddChunk(int documentId, int chunkIndex,
                  const std::string& text, const std::vector<float>& embedding,
                  std::string& err);
    void DeleteChunksForDocument(int documentId);
    int  ChunkCount(int documentId) const;

    // Cosine KNN (dot product; assumes L2-normalised embeddings, as nomic-embed-text produces).
    std::vector<CorpusSearchResult> Search(const std::vector<float>& query, int topK) const;

private:
    std::string m_dbPath;
    void*       m_db = nullptr;   // sqlite3* — kept opaque to avoid pulling sqlite3.h into every header

    bool EnsureSchema(std::string& err);
};
