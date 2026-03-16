#include "jarvisrag.h"
#include "../settings/jarvissettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QMimeDatabase>
#include <QRegularExpression>

#include <Baloo/Query>
#include <Baloo/ResultIterator>

#include <KFileMetaData/ExtractorCollection>
#include <KFileMetaData/Extractor>
#include <KFileMetaData/SimpleExtractionResult>

#include <cmath>

JarvisRag::JarvisRag(JarvisSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

const QStringList &JarvisRag::stopWords()
{
    static const QStringList words = {
        QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("is"),
        QStringLiteral("are"), QStringLiteral("was"), QStringLiteral("what"), QStringLiteral("how"),
        QStringLiteral("does"), QStringLiteral("do"), QStringLiteral("in"), QStringLiteral("of"),
        QStringLiteral("to"), QStringLiteral("for"), QStringLiteral("my"), QStringLiteral("me"),
        QStringLiteral("about"), QStringLiteral("from"), QStringLiteral("that"), QStringLiteral("this"),
        QStringLiteral("file"), QStringLiteral("document"), QStringLiteral("find"),
        QStringLiteral("search"), QStringLiteral("look"), QStringLiteral("read"),
        QStringLiteral("tell"), QStringLiteral("show"), QStringLiteral("give"),
        QStringLiteral("say"), QStringLiteral("says"), QStringLiteral("talk"),
        QStringLiteral("know"), QStringLiteral("think"),
        QStringLiteral("please"), QStringLiteral("can"), QStringLiteral("you"),
        QStringLiteral("just"), QStringLiteral("some"), QStringLiteral("like"),
        QStringLiteral("play"), QStringLiteral("open"), QStringLiteral("hey"),
    };
    return words;
}

QStringList JarvisRag::extractSearchTerms(const QString &query)
{
    const auto &sw = stopWords();
    QStringList terms;
    const auto words = query.toLower().split(QRegularExpression(QStringLiteral("\\W+")), Qt::SkipEmptyParts);
    for (const auto &word : words) {
        if (word.length() > 2 && !sw.contains(word))
            terms.append(word);
    }
    return terms;
}

QString JarvisRag::retrieveContext(const QString &query, int maxFiles, int maxCharsPerFile) const
{
    const QStringList searchTerms = extractSearchTerms(query);
    if (searchTerms.isEmpty()) return {};

    const QString searchString = searchTerms.join(QLatin1Char(' '));
    qDebug() << "[JARVIS] RAG search:" << searchString;

    // Search each term individually and score by match count.
    // Multi-word Baloo searches use AND logic which fails when content
    // indexing is incomplete, so we search per-term and rank by hits.
    QSet<QString> allPaths;
    for (const auto &term : searchTerms) {
        Baloo::Query q;
        q.setSearchString(term);
        q.setLimit(100);

        Baloo::ResultIterator it = q.exec();
        while (it.next()) {
            const QString path = it.filePath();
            if (path.contains(QStringLiteral("/build/"))
                || path.contains(QStringLiteral("/node_modules/"))
                || (path.contains(QStringLiteral("/."))
                    && !path.contains(QStringLiteral("/.config"))))
                continue;
            allPaths.insert(path);
        }
    }

    // Stage 1: Score each file by how many search terms match its path.
    // Uses exact substring match + fuzzy match (Levenshtein ≤ 2)
    // on individual path components to handle whisper mishearings.
    QMap<QString, int> fileScores;
    for (const auto &path : allPaths) {
        const QString lowerPath = path.toLower();
        const QStringList pathParts = lowerPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        int score = 0;

        for (const auto &term : searchTerms) {
            if (lowerPath.contains(term)) {
                ++score;
                continue;
            }
            if (term.length() >= 4) {
                for (const auto &part : pathParts) {
                    if (qAbs(term.length() - part.length()) > 2) continue;
                    const int tLen = term.length(), pLen = part.length();
                    QList<int> prev(pLen + 1), curr(pLen + 1);
                    for (int j = 0; j <= pLen; ++j) prev[j] = j;
                    for (int i = 1; i <= tLen; ++i) {
                        curr[0] = i;
                        for (int j = 1; j <= pLen; ++j) {
                            int cost = (term[i-1] == part[j-1]) ? 0 : 1;
                            curr[j] = std::min({prev[j]+1, curr[j-1]+1, prev[j-1]+cost});
                        }
                        std::swap(prev, curr);
                    }
                    if (prev[pLen] <= 2) { ++score; break; }
                }
            }
        }

        // Bonus: a single path component matching multiple search terms
        for (const auto &part : pathParts) {
            int partMatches = 0;
            for (const auto &term : searchTerms) {
                if (part.contains(term)) ++partMatches;
            }
            if (partMatches >= 2) score += partMatches;
        }

        if (score > 0)
            fileScores[path] = score;
    }

    // Sort by score, with README files getting a tiebreaker boost
    QList<QPair<int, QString>> ranked;
    for (auto it = fileScores.constBegin(); it != fileScores.constEnd(); ++it) {
        int bonus = 0;
        const QString lower = it.key().toLower();
        if (lower.endsWith(QStringLiteral("readme.md")) || lower.endsWith(QStringLiteral("readme")))
            bonus = 1;
        ranked.append({it.value() * 10 + bonus, it.key()});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });

    // Stage 1 filter: accept files with at least 1 term match (score >= 10).
    // Take top 20 candidates for content scoring.
    constexpr int maxCandidates = 20;
    QList<QPair<int, QString>> candidates;
    for (const auto &[score, filePath] : ranked) {
        if (candidates.size() >= maxCandidates) break;
        if (score < 10) continue;

        const QFileInfo info(filePath);
        if (info.size() > 5 * 1024 * 1024) continue;
        candidates.append({score, filePath});
    }

    // Stage 2: Content-based scoring on candidates.
    // Score by term frequency normalized by sqrt(doc length), times distinct terms found.
    m_textCache.clear();
    struct ContentScore { double score; QString path; };
    QList<ContentScore> contentRanked;

    for (const auto &[pathScore, filePath] : candidates) {
        const QString text = extractFileText(filePath, maxCharsPerFile * 2);
        if (text.isEmpty()) continue;

        m_textCache[filePath] = text;

        const QString lowerText = text.toLower();
        int totalHits = 0;
        int distinctTerms = 0;

        for (const auto &term : searchTerms) {
            int count = 0;
            int pos = 0;
            while ((pos = lowerText.indexOf(term, pos)) != -1) {
                ++count;
                pos += term.length();
            }
            if (count > 0) {
                totalHits += count;
                ++distinctTerms;
            }
        }

        if (distinctTerms == 0) {
            // No content match — only keep if path match was VERY strong (e.g. filename match)
            if (pathScore >= 20) { // Multiple terms matched in path
                contentRanked.append({pathScore * 0.05, filePath});
            }
            continue;
        }

        const double docLen = std::max(1.0, std::sqrt(static_cast<double>(lowerText.length())));
        const double score = (static_cast<double>(totalHits) / docLen) * distinctTerms;
        contentRanked.append({score, filePath});

        qDebug() << "[JARVIS] RAG: content score" << filePath
                 << "hits=" << totalHits << "distinct=" << distinctTerms
                 << "score=" << score;
    }

    std::sort(contentRanked.begin(), contentRanked.end(), [](const auto &a, const auto &b) {
        return a.score > b.score;
    });

    // Pick top results
    QString context;
    int fileCount = 0;
    for (const auto &[score, filePath] : contentRanked) {
        if (fileCount >= maxFiles) break;

        const QString &text = m_textCache.contains(filePath)
            ? m_textCache[filePath]
            : (m_textCache[filePath] = extractFileText(filePath, maxCharsPerFile * 2));
        if (text.isEmpty()) continue;

        const QString chunk = findRelevantChunk(text, searchString, maxCharsPerFile);
        if (chunk.trimmed().isEmpty()) continue;

        context += QStringLiteral("\n--- File: %1 ---\n%2\n").arg(filePath, chunk);
        ++fileCount;
    }

    m_textCache.clear();

    if (context.isEmpty()) {
        qDebug() << "[JARVIS] RAG: no relevant files found";
        return {};
    }

    qDebug() << "[JARVIS] RAG: found" << fileCount << "relevant files";
    return context.trimmed();
}

QString JarvisRag::extractFileText(const QString &filePath, int maxChars) const
{
    const QFileInfo info(filePath);
    if (info.isDir()) {
        const QDir dir(filePath);
        static const QStringList readmeNames = {
            QStringLiteral("README.md"), QStringLiteral("README.markdown"),
            QStringLiteral("README.txt"), QStringLiteral("README"),
            QStringLiteral("readme.md"), QStringLiteral("readme.txt"),
            QStringLiteral("readme")
        };
        for (const auto &name : readmeNames) {
            if (dir.exists(name)) {
                return extractFileText(dir.absoluteFilePath(name), maxChars);
            }
        }
        return {};
    }

    const QString mimeType = QMimeDatabase().mimeTypeForFile(filePath).name();

    // Only process document/text MIME types — skip audio, video, images, etc.
    if (!mimeType.startsWith(QStringLiteral("text/"))
        && !mimeType.startsWith(QStringLiteral("application/pdf"))
        && !mimeType.startsWith(QStringLiteral("application/vnd.oasis.opendocument"))
        && !mimeType.startsWith(QStringLiteral("application/vnd.openxmlformats-officedocument"))
        && !mimeType.startsWith(QStringLiteral("application/msword"))
        && !mimeType.startsWith(QStringLiteral("application/vnd.ms-"))
        && !mimeType.startsWith(QStringLiteral("application/rtf"))
        && !mimeType.startsWith(QStringLiteral("application/json"))
        && !mimeType.startsWith(QStringLiteral("application/xml"))
        && !mimeType.startsWith(QStringLiteral("application/x-yaml"))
        && !mimeType.startsWith(QStringLiteral("application/toml"))
        && !mimeType.startsWith(QStringLiteral("application/x-shellscript"))
        && !mimeType.startsWith(QStringLiteral("application/x-desktop"))
        && !mimeType.startsWith(QStringLiteral("application/epub"))) {
        return {};
    }

    // Try KFileMetaData extractors first (handles PDF, ODT, DOCX, etc.)
    auto extractors = m_extractorCollection.fetchExtractors(mimeType);
    if (!extractors.isEmpty()) {
        KFileMetaData::SimpleExtractionResult result(filePath, mimeType,
            KFileMetaData::ExtractionResult::ExtractPlainText);
        extractors.first()->extract(&result);
        QString text = result.text().left(maxChars);
        if (!text.trimmed().isEmpty()) {
            qDebug() << "[JARVIS] RAG: extracted via KFileMetaData:" << filePath;
            return text;
        }
    }

    // Fallback: direct read for plain text files
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
        // No matches — if the text is short, we can return it all, 
        // but if it's long and doesn't match, return empty to skip it.
        return text.length() <= maxChars ? text : QString();
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
    const int startLine = qMax(0, bestIdx - 5);
    for (int i = startLine; i < lines.size() && chars < maxChars; ++i) {
        chunk += lines[i] + QLatin1Char('\n');
        chars += lines[i].length() + 1;
    }

    return chunk;
}
