#include "jarvisrag.h"
#include "../settings/jarvissettings.h"

#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>

#include <Baloo/Query>
#include <Baloo/ResultIterator>

JarvisRag::JarvisRag(JarvisSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}


QString JarvisRag::retrieveContext(const QString &query, int maxFiles, int maxCharsPerFile) const
{
    // Extract search terms — remove common stop words
    static const QStringList stopWords = {
        QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("is"),
        QStringLiteral("are"), QStringLiteral("was"), QStringLiteral("what"), QStringLiteral("how"),
        QStringLiteral("does"), QStringLiteral("do"), QStringLiteral("in"), QStringLiteral("of"),
        QStringLiteral("to"), QStringLiteral("for"), QStringLiteral("my"), QStringLiteral("me"),
        QStringLiteral("about"), QStringLiteral("from"), QStringLiteral("that"), QStringLiteral("this"),
        QStringLiteral("file"), QStringLiteral("document"), QStringLiteral("find"),
        QStringLiteral("search"), QStringLiteral("look"), QStringLiteral("read"),
        QStringLiteral("tell"), QStringLiteral("say"), QStringLiteral("says"),
        QStringLiteral("please"), QStringLiteral("can"), QStringLiteral("you"),
    };

    QStringList words = query.toLower().split(QRegularExpression(QStringLiteral("\\W+")), Qt::SkipEmptyParts);
    QStringList searchTerms;
    for (const auto &word : words) {
        if (word.length() > 2 && !stopWords.contains(word))
            searchTerms.append(word);
    }

    if (searchTerms.isEmpty()) return {};

    const QString searchString = searchTerms.join(QLatin1Char(' '));
    qDebug() << "[JARVIS] RAG search:" << searchString;

    // Query Baloo
    Baloo::Query balooQuery;
    balooQuery.setSearchString(searchString);
    balooQuery.setLimit(static_cast<uint>(maxFiles * 2)); // Get more results to filter
    balooQuery.addType(QStringLiteral("Document"));

    Baloo::ResultIterator it = balooQuery.exec();

    QString context;
    int fileCount = 0;

    while (it.next() && fileCount < maxFiles) {
        const QString filePath = it.filePath();
        const QFileInfo info(filePath);

        // Skip binary files, very large files, hidden directories
        if (info.size() > 5 * 1024 * 1024) continue; // >5MB
        if (filePath.contains(QStringLiteral("/.")) && !filePath.contains(QStringLiteral("/.config"))) continue;

        const QString text = extractFileText(filePath, maxCharsPerFile * 2);
        if (text.isEmpty()) continue;

        const QString chunk = findRelevantChunk(text, searchString, maxCharsPerFile);
        if (chunk.isEmpty()) continue;

        context += QStringLiteral("\n--- File: %1 ---\n%2\n").arg(filePath, chunk);
        ++fileCount;
    }

    // If no Document type results, try without type filter
    if (fileCount == 0) {
        Baloo::Query fallbackQuery;
        fallbackQuery.setSearchString(searchString);
        fallbackQuery.setLimit(static_cast<uint>(maxFiles * 2));

        Baloo::ResultIterator fit = fallbackQuery.exec();
        while (fit.next() && fileCount < maxFiles) {
            const QString filePath = fit.filePath();
            const QFileInfo info(filePath);

            if (info.size() > 5 * 1024 * 1024) continue;
            if (filePath.contains(QStringLiteral("/.")) && !filePath.contains(QStringLiteral("/.config"))) continue;

            const QString text = extractFileText(filePath, maxCharsPerFile * 2);
            if (text.isEmpty()) continue;

            const QString chunk = findRelevantChunk(text, searchString, maxCharsPerFile);
            if (chunk.isEmpty()) continue;

            context += QStringLiteral("\n--- File: %1 ---\n%2\n").arg(filePath, chunk);
            ++fileCount;
        }
    }

    if (context.isEmpty()) {
        qDebug() << "[JARVIS] RAG: no relevant files found";
        return {};
    }

    qDebug() << "[JARVIS] RAG: found" << fileCount << "relevant files";
    return context.trimmed();
}

QString JarvisRag::extractFileText(const QString &filePath, int maxChars)
{
    const QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();

    // Only handle text-based files
    static const QStringList textSuffixes = {
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("markdown"),
        QStringLiteral("rst"), QStringLiteral("org"), QStringLiteral("csv"),
        QStringLiteral("json"), QStringLiteral("yaml"), QStringLiteral("yml"),
        QStringLiteral("toml"), QStringLiteral("ini"), QStringLiteral("conf"),
        QStringLiteral("cfg"), QStringLiteral("xml"), QStringLiteral("html"),
        QStringLiteral("htm"), QStringLiteral("css"), QStringLiteral("js"),
        QStringLiteral("ts"), QStringLiteral("py"), QStringLiteral("rb"),
        QStringLiteral("go"), QStringLiteral("rs"), QStringLiteral("c"),
        QStringLiteral("cpp"), QStringLiteral("h"), QStringLiteral("hpp"),
        QStringLiteral("java"), QStringLiteral("kt"), QStringLiteral("sh"),
        QStringLiteral("bash"), QStringLiteral("zsh"), QStringLiteral("fish"),
        QStringLiteral("cmake"), QStringLiteral("makefile"),
        QStringLiteral("dockerfile"), QStringLiteral("tf"),
        QStringLiteral("hcl"), QStringLiteral("nix"), QStringLiteral("qml"),
        QStringLiteral("tex"), QStringLiteral("log"), QStringLiteral("env"),
    };

    // Files without suffix might be text (README, Makefile, etc.)
    if (!suffix.isEmpty() && !textSuffixes.contains(suffix)) return {};

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};

    const QByteArray raw = file.read(maxChars);

    // Quick binary check — if >10% non-printable, skip
    int nonPrintable = 0;
    for (const char c : raw) {
        if (c != '\n' && c != '\r' && c != '\t' && (c < 32 || c > 126))
            ++nonPrintable;
    }
    if (raw.size() > 0 && nonPrintable > raw.size() / 10) return {};

    return QString::fromUtf8(raw);
}

QString JarvisRag::findRelevantChunk(const QString &text, const QString &query, int maxChars)
{
    if (text.length() <= maxChars) return text;

    // Find the section of the text most relevant to the query
    const QStringList terms = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QStringList lines = text.split(QLatin1Char('\n'));

    // Score each line by how many query terms it contains
    struct ScoredLine { int index; int score; };
    QVector<ScoredLine> scored;
    scored.reserve(lines.size());

    for (int i = 0; i < lines.size(); ++i) {
        int score = 0;
        const QString lower = lines[i].toLower();
        for (const auto &term : terms) {
            if (lower.contains(term)) ++score;
        }
        if (score > 0) scored.append({i, score});
    }

    if (scored.isEmpty()) {
        // No matches — return the beginning
        return text.left(maxChars);
    }

    // Find the highest-scoring line
    int bestIdx = 0;
    int bestScore = 0;
    for (const auto &s : scored) {
        if (s.score > bestScore) {
            bestScore = s.score;
            bestIdx = s.index;
        }
    }

    // Extract a window around the best match
    QString chunk;
    int chars = 0;
    // Start a few lines before the best match
    const int startLine = qMax(0, bestIdx - 5);
    for (int i = startLine; i < lines.size() && chars < maxChars; ++i) {
        chunk += lines[i] + QLatin1Char('\n');
        chars += lines[i].length() + 1;
    }

    return chunk;
}
