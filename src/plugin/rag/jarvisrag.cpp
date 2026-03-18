#include "jarvisrag.h"
#include "../settings/jarvissettings.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

#include <Baloo/Query>
#include <Baloo/ResultIterator>

#include <KFileMetaData/ExtractorCollection>
#include <KFileMetaData/Extractor>
#include <KFileMetaData/SimpleExtractionResult>

#include <cmath>

static float cosineSimilarity(const QVector<float> &a, const QVector<float> &b)
{
    if (a.size() != b.size() || a.isEmpty()) return 0.0f;
    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for (int i = 0; i < a.size(); ++i) {
        dot   += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
    const float denom = std::sqrt(normA) * std::sqrt(normB);
    return denom > 0.0f ? dot / denom : 0.0f;
}

JarvisRag::JarvisRag(JarvisSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

const QStringList &JarvisRag::stopWords()
{
    static const QStringList words = {
        // Articles, prepositions, pronouns
        QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("is"),
        QStringLiteral("are"), QStringLiteral("was"), QStringLiteral("were"), QStringLiteral("been"),
        QStringLiteral("being"), QStringLiteral("have"), QStringLiteral("has"), QStringLiteral("had"),
        QStringLiteral("does"), QStringLiteral("do"), QStringLiteral("did"), QStringLiteral("will"),
        QStringLiteral("would"), QStringLiteral("could"), QStringLiteral("should"), QStringLiteral("shall"),
        QStringLiteral("might"), QStringLiteral("must"), QStringLiteral("may"),
        QStringLiteral("in"), QStringLiteral("of"), QStringLiteral("to"), QStringLiteral("for"),
        QStringLiteral("with"), QStringLiteral("at"), QStringLiteral("by"), QStringLiteral("on"),
        QStringLiteral("from"), QStringLiteral("into"), QStringLiteral("through"), QStringLiteral("between"),
        QStringLiteral("but"), QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("not"),
        QStringLiteral("no"), QStringLiteral("nor"), QStringLiteral("so"), QStringLiteral("yet"),
        QStringLiteral("my"), QStringLiteral("me"), QStringLiteral("you"), QStringLiteral("your"),
        QStringLiteral("he"), QStringLiteral("she"), QStringLiteral("it"), QStringLiteral("its"),
        QStringLiteral("we"), QStringLiteral("they"), QStringLiteral("them"), QStringLiteral("their"),
        QStringLiteral("our"), QStringLiteral("his"), QStringLiteral("her"),
        // Question words, conversational
        QStringLiteral("what"), QStringLiteral("how"), QStringLiteral("why"), QStringLiteral("when"),
        QStringLiteral("where"), QStringLiteral("which"), QStringLiteral("who"), QStringLiteral("whom"),
        QStringLiteral("that"), QStringLiteral("this"), QStringLiteral("these"), QStringLiteral("those"),
        QStringLiteral("about"), QStringLiteral("there"), QStringLiteral("here"), QStringLiteral("than"),
        // Common verbs that don't indicate file relevance
        QStringLiteral("tell"), QStringLiteral("show"), QStringLiteral("give"), QStringLiteral("get"),
        QStringLiteral("make"), QStringLiteral("take"), QStringLiteral("come"), QStringLiteral("go"),
        QStringLiteral("say"), QStringLiteral("says"), QStringLiteral("said"), QStringLiteral("talk"),
        QStringLiteral("know"), QStringLiteral("think"), QStringLiteral("want"), QStringLiteral("need"),
        QStringLiteral("feel"), QStringLiteral("see"), QStringLiteral("look"), QStringLiteral("find"),
        QStringLiteral("use"), QStringLiteral("try"), QStringLiteral("ask"), QStringLiteral("work"),
        QStringLiteral("call"), QStringLiteral("keep"), QStringLiteral("let"), QStringLiteral("put"),
        QStringLiteral("run"), QStringLiteral("set"), QStringLiteral("turn"), QStringLiteral("help"),
        QStringLiteral("start"), QStringLiteral("stop"), QStringLiteral("move"),
        // Filler / conversational
        QStringLiteral("please"), QStringLiteral("can"), QStringLiteral("just"), QStringLiteral("some"),
        QStringLiteral("like"), QStringLiteral("also"), QStringLiteral("well"), QStringLiteral("very"),
        QStringLiteral("really"), QStringLiteral("much"), QStringLiteral("more"), QStringLiteral("most"),
        QStringLiteral("only"), QStringLiteral("even"), QStringLiteral("still"), QStringLiteral("already"),
        QStringLiteral("ever"), QStringLiteral("never"), QStringLiteral("always"), QStringLiteral("often"),
        QStringLiteral("maybe"), QStringLiteral("probably"), QStringLiteral("actually"),
        // Commands that shouldn't trigger file search
        QStringLiteral("play"), QStringLiteral("open"), QStringLiteral("hey"),
        QStringLiteral("file"), QStringLiteral("document"), QStringLiteral("search"), QStringLiteral("read"),
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
    if (searchTerms.size() < 2) return {};  // Need at least 2 meaningful terms to avoid noise

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

    // Pick top results — require a meaningful content score.
    // A single common word matching a few times in a large file is noise.
    // Require at least 2 distinct search terms to match, or a high score
    // from concentrated matches of a single distinctive term.
    constexpr double minContentScore = 1.0;

    // Filter by minimum content score for Stage 3
    QList<ContentScore> passedContent;
    for (const auto &cs : contentRanked) {
        if (cs.score < minContentScore) break; // Sorted
        passedContent.append(cs);
    }

    // Stage 3: Embedding-based re-ranking via Ollama
    m_chunkCache.clear();
    const QString embeddingModel = m_settings->embeddingModel();
    if (!embeddingModel.isEmpty() && passedContent.size() > 1) {
        constexpr int maxEmbedCandidates = 10;
        const int embedCount = std::min(passedContent.size(), static_cast<qsizetype>(maxEmbedCandidates));

        // Build texts: query at index 0, candidate chunks at 1..N
        QStringList embedTexts;
        embedTexts.append(query);
        for (int i = 0; i < embedCount; ++i) {
            const QString &filePath = passedContent[i].path;
            const QString &text = m_textCache.contains(filePath)
                ? m_textCache[filePath]
                : (m_textCache[filePath] = extractFileText(filePath, maxCharsPerFile * 2));
            QString chunk = findRelevantChunk(text, searchString, maxCharsPerFile);
            m_chunkCache[filePath] = chunk;
            // Truncate chunk for embedding to keep request size reasonable
            embedTexts.append(chunk.left(2000));
        }

        const auto embeddings = fetchEmbeddings(embedTexts);
        if (embeddings.size() == embedTexts.size()) {
            const auto &queryEmbed = embeddings[0];
            for (int i = 0; i < embedCount; ++i) {
                float sim = cosineSimilarity(queryEmbed, embeddings[i + 1]);
                qDebug() << "[JARVIS] RAG: embedding score" << passedContent[i].path
                         << "sim=" << sim;
                // Replace content score with cosine similarity for re-ranking
                passedContent[i].score = sim;
            }
            // Re-sort by embedding similarity
            std::sort(passedContent.begin(), passedContent.begin() + embedCount,
                      [](const auto &a, const auto &b) { return a.score > b.score; });
        } else {
            qWarning() << "[JARVIS] RAG: embedding fetch failed, keeping term-frequency ranking";
        }
    }

    QString context;
    int fileCount = 0;
    for (const auto &[score, filePath] : passedContent) {
        if (fileCount >= maxFiles) break;

        const QString &text = m_textCache.contains(filePath)
            ? m_textCache[filePath]
            : (m_textCache[filePath] = extractFileText(filePath, maxCharsPerFile * 2));
        if (text.isEmpty()) continue;

        // Use cached chunk from embedding stage if available
        const QString chunk = m_chunkCache.contains(filePath)
            ? m_chunkCache[filePath]
            : findRelevantChunk(text, searchString, maxCharsPerFile);
        if (chunk.trimmed().isEmpty()) continue;

        context += QStringLiteral("\n--- File: %1 ---\n%2\n").arg(filePath, chunk);
        ++fileCount;
    }

    m_textCache.clear();
    m_chunkCache.clear();

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

QList<QVector<float>> JarvisRag::fetchEmbeddings(const QStringList &texts) const
{
    if (texts.isEmpty()) return {};

    const QString model = m_settings->embeddingModel();
    const QString serverUrl = m_settings->embeddingServerUrl();
    if (model.isEmpty() || serverUrl.isEmpty()) return {};

    // Build request payload: {"model": "...", "input": ["...", ...]}
    QJsonArray inputArray;
    for (const auto &t : texts)
        inputArray.append(t);

    QJsonObject payload;
    payload[QStringLiteral("model")] = model;
    payload[QStringLiteral("input")] = inputArray;

    const QUrl url(serverUrl + QStringLiteral("/api/embed"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    // Thread-local QNAM since RAG runs in QtConcurrent::run
    thread_local QNetworkAccessManager nam;
    QNetworkReply *reply = nam.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    // Block with event loop + timeout
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(5000);
    loop.exec();

    QList<QVector<float>> result;

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        qWarning() << "[JARVIS] RAG: embedding request timed out";
        return result;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[JARVIS] RAG: embedding request failed:" << reply->errorString();
        reply->deleteLater();
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();

    const QJsonArray embeddings = doc.object()[QStringLiteral("embeddings")].toArray();
    if (embeddings.size() != texts.size()) {
        qWarning() << "[JARVIS] RAG: embedding count mismatch, expected"
                    << texts.size() << "got" << embeddings.size();
        return result;
    }

    result.reserve(embeddings.size());
    for (const auto &embVal : embeddings) {
        const QJsonArray vec = embVal.toArray();
        QVector<float> embedding(vec.size());
        for (int i = 0; i < vec.size(); ++i)
            embedding[i] = static_cast<float>(vec[i].toDouble());
        result.append(std::move(embedding));
    }

    return result;
}
