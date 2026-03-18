#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

#include <KFileMetaData/ExtractorCollection>

class JarvisSettings;

/// Retrieval-Augmented Generation using KDE Baloo file indexer.
/// Searches indexed files and extracts relevant text chunks for LLM context.
class JarvisRag : public QObject
{
    Q_OBJECT

public:
    explicit JarvisRag(JarvisSettings *settings, QObject *parent = nullptr);

    /// Consolidated stop words used by both RAG and backend search.
    [[nodiscard]] static const QStringList &stopWords();

    /// Extract meaningful search terms from a natural-language query.
    [[nodiscard]] static QStringList extractSearchTerms(const QString &query);

    /// Search Baloo for files matching the query, extract relevant chunks.
    /// Returns formatted context string ready for injection into the LLM prompt.
    [[nodiscard]] QString retrieveContext(const QString &query, int maxFiles = 5, int maxCharsPerFile = 4000) const;

private:
    /// Extract text content from a file using KFileMetaData or plain read.
    [[nodiscard]] QString extractFileText(const QString &filePath, int maxChars) const;

    /// Find the most relevant chunk within a file's text for the given query.
    [[nodiscard]] static QString findRelevantChunk(const QString &text, const QString &query, int maxChars);

    /// Fetch embedding vectors from Ollama for a batch of texts.
    [[nodiscard]] QList<QVector<float>> fetchEmbeddings(const QStringList &texts) const;

    JarvisSettings *m_settings{nullptr};
    KFileMetaData::ExtractorCollection m_extractorCollection;
    mutable QHash<QString, QString> m_textCache;
    mutable QHash<QString, QString> m_chunkCache;
};
