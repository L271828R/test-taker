#pragma once
#include <string>
#include <vector>

// Split text into overlapping word-window chunks.
// windowWords: target words per chunk; overlapWords: words shared with the previous chunk.
std::vector<std::string> ChunkText(const std::string& text,
                                    int windowWords  = 350,
                                    int overlapWords = 50);

// Returns true if the chunk has enough alphabetic content to be worth embedding.
// Rejects chunks that are too short or dominated by digits/symbols (e.g. PDF line numbers).
bool IsUsefulChunk(const std::string& chunk, float minAlphaRatio = 0.50f, int minWords = 20);

struct EmbedResult {
    bool ok = false;
    std::vector<float> embedding;
    std::string error;
};

// POST to Ollama /api/embeddings; returns the float embedding vector.
// Synchronous — call from a background thread.
EmbedResult EmbedText(const std::string& text,
                       const std::string& ollamaUrl = "http://localhost:11434",
                       const std::string& model     = "nomic-embed-text");

// Shell out to pdftotext to extract text from a PDF.
// Returns extracted text, or empty string on failure (err is set).
std::string ExtractPDF(const std::string& pdfPath, std::string& err);
