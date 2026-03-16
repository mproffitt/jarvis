#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class JarvisSettings;

/// Retrieval-Augmented Generation using KDE Baloo file indexer.
/// Searches indexed files and extracts relevant text chunks for LLM context.
class JarvisRag : public QObject
{
    Q_OBJECT

public:
    explicit JarvisRag(JarvisSettings *settings, QObject *parent = nullptr);

    /// Search Baloo for files matching the query, extract relevant chunks.
    /// Returns formatted context string ready for injection into the LLM prompt.
    [[nodiscard]] QString retrieveContext(const QString &query, int maxFiles = 5, int maxCharsPerFile = 2000) const;

private:
    /// Extract text content from a file (plain text, markdown, PDF text, etc.)
    [[nodiscard]] static QString extractFileText(const QString &filePath, int maxChars);

    /// Find the most relevant chunk within a file's text for the given query.
    [[nodiscard]] static QString findRelevantChunk(const QString &text, const QString &query, int maxChars);

    JarvisSettings *m_settings{nullptr};
};
