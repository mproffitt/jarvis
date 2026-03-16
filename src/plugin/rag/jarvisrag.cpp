#include "jarvisrag.h"
#include "../settings/jarvissettings.h"

#include <QDir>
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

    // Search each term individually and score by match count.
    // Multi-word Baloo searches use AND logic which fails when content
    // indexing is incomplete, so we search per-term and rank by hits.
    QSet<QString> allPaths;
    for (const auto &term : searchTerms) {
        Baloo::Query q;
        q.setSearchString(term);
        q.setLimit(static_cast<uint>(maxFiles * 10));

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

    // Score each file by how many search terms match its path.
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
            // Fuzzy match against path components (edit distance ≤ 2)
            if (term.length() >= 4) {
                for (const auto &part : pathParts) {
                    if (qAbs(term.length() - part.length()) > 2) continue;
                    // Levenshtein distance
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
        // At least 1 point for being in Baloo results at all
        fileScores[path] = qMax(score, 1);
    }

    // Sort by score (most matching terms in path first)
    QList<QPair<int, QString>> ranked;
    for (auto it = fileScores.constBegin(); it != fileScores.constEnd(); ++it)
        ranked.append({it.value(), it.key()});
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });

    // Only include files that match at least 2 search terms in their path.
    // Single-term matches are too noisy (generic words match everything).
    QString context;
    int fileCount = 0;
    for (const auto &[score, filePath] : ranked) {
        if (fileCount >= maxFiles) break;
        if (score < 2) continue; // Skip weak matches

        const QFileInfo info(filePath);
        if (info.size() > 5 * 1024 * 1024) continue;

        const QString text = extractFileText(filePath, maxCharsPerFile * 2);
        if (text.isEmpty()) continue;

        const QString chunk = findRelevantChunk(text, searchString, maxCharsPerFile);
        if (chunk.isEmpty()) continue;

        context += QStringLiteral("\n--- File: %1 ---\n%2\n").arg(filePath, chunk);
        ++fileCount;
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
    if (!suffix.isEmpty() && !textSuffixes.contains(suffix.toLower())) return {};

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
