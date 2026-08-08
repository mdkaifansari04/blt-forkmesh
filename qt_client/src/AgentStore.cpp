#include "AgentStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QVariant>

#include <algorithm>
#include <utility>

namespace {

QString sanitize(const QString &owner, const QString &name)
{
    QString key = owner + QStringLiteral("-") + name;
    for (QChar &c : key)
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('.'))
            c = QLatin1Char('_');
    return key;
}

} // namespace

QString AgentSession::repoKey() const
{
    return sanitize(owner, name);
}

QJsonObject AgentSession::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["owner"] = owner;
    obj["name"] = name;
    obj["issueNumber"] = issueNumber;
    obj["issueTitle"] = issueTitle;
    obj["prompt"] = prompt;
    obj["provider"] = provider;
    obj["model"] = model;
    obj["mode"] = mode;
    obj["createPr"] = createPr;
    obj["associationOnly"] = associationOnly;
    obj["yolo"] = yolo;
    obj["genie"] = genie;
    obj["strength"] = strength;
    obj["orgTask"] = orgTask;
    obj["orgTaskId"] = orgTaskId;
    obj["startedByBot"] = startedByBot;
    obj["finishedByBot"] = finishedByBot;
    obj["orgAgentJob"] = orgAgentJob;
    obj["prNumber"] = prNumber;
    obj["status"] = status;
    obj["branchName"] = branchName;
    obj["baseRef"] = baseRef;
    obj["baseBranch"] = baseBranch;
    obj["mergeCandidateHead"] = mergeCandidateHead;
    obj["merged"] = merged;
    obj["mergedAtMs"] = mergedAtMs;
    obj["createdAtMs"] = createdAtMs;
    obj["startedAtMs"] = startedAtMs;
    obj["finishedAtMs"] = finishedAtMs;
    obj["promptTokens"] = promptTokens;
    obj["completionTokens"] = completionTokens;
    obj["totalTokens"] = totalTokens;
    obj["contextTokens"] = contextTokens;
    obj["contextWindow"] = contextWindow;
    obj["maxOutputTokens"] = maxOutputTokens;
    obj["estimatedCredits"] = estimatedCredits;
    obj["costUsd"] = costUsd;
    obj["spendBeforeUsd"] = spendBeforeUsd;
    obj["spendAfterUsd"] = spendAfterUsd;
    obj["numTurns"] = numTurns;
    obj["durationMs"] = durationMs;
    obj["lastError"] = lastError;
    return obj;
}

AgentSession AgentSession::fromJson(const QJsonObject &obj)
{
    AgentSession session;
    session.id = obj.value("id").toInt();
    session.owner = obj.value("owner").toString();
    session.name = obj.value("name").toString();
    session.issueNumber = obj.value("issueNumber").toInt();
    session.issueTitle = obj.value("issueTitle").toString();
    session.prompt = obj.value("prompt").toString();
    session.provider = obj.value("provider").toString();
    session.model = obj.value("model").toString();
    session.mode = obj.value("mode").toString();
    session.createPr = obj.value("createPr").toBool();
    session.associationOnly = obj.value("associationOnly").toBool();
    session.yolo = obj.value("yolo").toBool();
    session.genie = obj.value("genie").toBool();
    session.strength = obj.value("strength").toString();
    session.orgTask = obj.value("orgTask").toBool();
    session.orgTaskId = obj.value("orgTaskId").toString();
    session.startedByBot = obj.value("startedByBot").toString();
    session.finishedByBot = obj.value("finishedByBot").toString();
    session.orgAgentJob = obj.value("orgAgentJob").toObject();
    session.prNumber = obj.value("prNumber").toInt();
    session.status = obj.value("status").toString(AgentStatus::Queued);
    session.branchName = obj.value("branchName").toString();
    session.baseRef = obj.value("baseRef").toString();
    session.baseBranch = obj.value("baseBranch").toString();
    session.mergeCandidateHead = obj.value("mergeCandidateHead").toString();
    session.merged = obj.value("merged").toBool();
    session.mergedAtMs = obj.value("mergedAtMs").toVariant().toLongLong();
    session.createdAtMs = obj.value("createdAtMs").toVariant().toLongLong();
    session.startedAtMs = obj.value("startedAtMs").toVariant().toLongLong();
    session.finishedAtMs = obj.value("finishedAtMs").toVariant().toLongLong();
    session.promptTokens = obj.value("promptTokens").toInt();
    session.completionTokens = obj.value("completionTokens").toInt();
    session.totalTokens = obj.value("totalTokens").toInt();
    session.contextTokens = obj.value("contextTokens").toInt();
    session.contextWindow = obj.value("contextWindow").toInt();
    session.maxOutputTokens = obj.value("maxOutputTokens").toInt();
    session.estimatedCredits = obj.value("estimatedCredits").toInt();
    session.costUsd = obj.value("costUsd").toDouble();
    session.spendBeforeUsd = obj.value("spendBeforeUsd").toDouble();
    session.spendAfterUsd = obj.value("spendAfterUsd").toDouble();
    session.numTurns = obj.value("numTurns").toInt();
    session.durationMs = obj.value("durationMs").toVariant().toLongLong();
    session.lastError = obj.value("lastError").toString();
    return session;
}

AgentStore::AgentStore(QString rootDir) : m_root(std::move(rootDir))
{
    QDir().mkpath(sessionsDir());
}

QString AgentStore::sessionsDir() const
{
    return m_root + QStringLiteral("/sessions");
}

QString AgentStore::sessionDir(const AgentSession &session) const
{
    return sessionsDir() + QLatin1Char('/') + session.repoKey() +
           QLatin1Char('/') + QString::number(session.id);
}

int AgentStore::nextId() const
{
    int maxId = 0;
    QDir root(sessionsDir());
    const QStringList repos =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &repo : repos) {
        const QStringList ids = QDir(root.filePath(repo))
                                    .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &id : ids)
            maxId = qMax(maxId, id.toInt());
    }
    return maxId + 1;
}

AgentSession AgentStore::createSession(AgentSession session)
{
    session.id = nextId();
    session.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    QDir().mkpath(sessionDir(session));
    saveSession(session);
    return session;
}

bool AgentStore::saveSession(const AgentSession &session) const
{
    QDir().mkpath(sessionDir(session));
    QFile file(sessionDir(session) + QStringLiteral("/meta.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(session.toJson()).toJson(QJsonDocument::Indented));
    return true;
}

bool AgentStore::deleteSession(const AgentSession &session) const
{
    QDir dir(sessionDir(session));
    // Nothing on disk means nothing to retire: a second delete of the same
    // session must not credit its model twice.
    if (!dir.exists())
        return true;
    if (!dir.removeRecursively())
        return false;
    recordRetiredSession(session);
    return true;
}

QString AgentStore::modelOutcomeKey(const QString &provider, const QString &model)
{
    const QString id = model.trimmed().toLower();
    if (id.isEmpty())
        return QString();
    return provider + QLatin1Char('\x1f') + id;
}

QString AgentStore::modelOutcomesPath() const
{
    return m_root + QStringLiteral("/model-outcomes.json");
}

QHash<QString, AgentModelOutcome> AgentStore::retiredModelOutcomes() const
{
    QHash<QString, AgentModelOutcome> outcomes;
    QFile file(modelOutcomesPath());
    if (!file.open(QIODevice::ReadOnly))
        return outcomes;
    const QJsonArray models =
        QJsonDocument::fromJson(file.readAll()).object().value("models").toArray();
    for (const QJsonValue &value : models) {
        const QJsonObject entry = value.toObject();
        const QString key = modelOutcomeKey(entry.value("provider").toString(),
                                            entry.value("model").toString());
        if (key.isEmpty())
            continue;
        AgentModelOutcome &outcome = outcomes[key];
        outcome.runs += qMax(0, entry.value("runs").toInt());
        outcome.merged += qMax(0, entry.value("merged").toInt());
    }
    return outcomes;
}

// Fold one about-to-vanish session into the durable tally. Read-modify-write of
// a file this small is cheaper than any index, and writing through QSaveFile
// keeps a crash mid-delete from truncating the whole history.
void AgentStore::recordRetiredSession(const AgentSession &session) const
{
    const QString key = modelOutcomeKey(session.provider, session.model);
    if (key.isEmpty())
        return;
    QJsonArray models;
    QFile existing(modelOutcomesPath());
    if (existing.open(QIODevice::ReadOnly)) {
        models = QJsonDocument::fromJson(existing.readAll())
                     .object()
                     .value("models")
                     .toArray();
        existing.close();
    }
    bool credited = false;
    for (int i = 0; i < models.size() && !credited; ++i) {
        QJsonObject entry = models.at(i).toObject();
        if (modelOutcomeKey(entry.value("provider").toString(),
                            entry.value("model").toString()) != key)
            continue;
        entry["runs"] = qMax(0, entry.value("runs").toInt()) + 1;
        entry["merged"] =
            qMax(0, entry.value("merged").toInt()) + (session.merged ? 1 : 0);
        models.replace(i, entry);
        credited = true;
    }
    if (!credited) {
        models.append(QJsonObject{
            {"provider", session.provider},
            {"model", session.model.trimmed().toLower()},
            {"runs", 1},
            {"merged", session.merged ? 1 : 0},
        });
    }
    QDir().mkpath(m_root);
    QSaveFile file(modelOutcomesPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(QJsonObject{{"version", 1}, {"models", models}})
                   .toJson(QJsonDocument::Indented));
    file.commit();
}

void AgentStore::appendLog(const AgentSession &session, const QString &text) const
{
    QDir().mkpath(sessionDir(session));
    QFile file(sessionDir(session) + QStringLiteral("/transcript.txt"));
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    file.write(text.toUtf8());
    if (!text.endsWith(QLatin1Char('\n')))
        file.write("\n");
}

QString AgentStore::readLog(const AgentSession &session) const
{
    QFile file(sessionDir(session) + QStringLiteral("/transcript.txt"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

void AgentStore::appendEvent(const AgentSession &session, const QJsonObject &ev) const
{
    QDir().mkpath(sessionDir(session));
    QFile file(sessionDir(session) + QStringLiteral("/events.jsonl"));
    if (!file.open(QIODevice::Append))
        return;
    file.write(QJsonDocument(ev).toJson(QJsonDocument::Compact));
    file.write("\n");
}

QList<QJsonObject> AgentStore::loadEvents(const AgentSession &session) const
{
    QList<QJsonObject> events;
    QFile file(sessionDir(session) + QStringLiteral("/events.jsonl"));
    if (!file.open(QIODevice::ReadOnly))
        return events;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject())
            events.append(doc.object());
    }
    return events;
}

void AgentStore::clearEvents(const AgentSession &session) const
{
    QFile::remove(sessionDir(session) + QStringLiteral("/events.jsonl"));
}

void AgentStore::writePatch(const AgentSession &session, const QString &patch) const
{
    QDir().mkpath(sessionDir(session));
    QFile file(sessionDir(session) + QStringLiteral("/changes.patch"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    file.write(patch.toUtf8());
}

QString AgentStore::readPatch(const AgentSession &session) const
{
    QFile file(sessionDir(session) + QStringLiteral("/changes.patch"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

namespace {

// Read the tail of a transcript file, dropping the first (partial) line so the
// scan never starts mid-line — or mid-UTF-8-character.
QString readTranscriptTail(const QString &path, qint64 tailBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const qint64 size = file.size();
    if (size > tailBytes) {
        file.seek(size - tailBytes);
        file.readLine();
    }
    return QString::fromUtf8(file.readAll());
}

// Both ends of a transcript file, whole lines only: the first `endBytes` and the
// last `endBytes`, with the middle of a long run skipped. An attachment is named
// in a prompt, and a session's prompts are either the opening turn (head) or the
// latest follow-up (tail) — so reading the ends finds them without pulling a
// multi-megabyte transcript through memory on every scan.
QString readTranscriptEnds(const QString &path, qint64 endBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const qint64 size = file.size();
    if (size <= 2 * endBytes)
        return QString::fromUtf8(file.readAll());
    QByteArray head = file.read(endBytes);
    // Drop the trailing partial line, which also keeps the cut on a UTF-8
    // character boundary.
    const qsizetype lastNewline = head.lastIndexOf('\n');
    head.truncate(lastNewline < 0 ? 0 : lastNewline + 1);
    file.seek(size - endBytes);
    file.readLine(); // …and the leading partial line of the tail
    return QString::fromUtf8(head) + QString::fromUtf8(file.readAll());
}

// Size + modified time of a file, or an empty stamp when it isn't there.
QString fileStamp(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return QStringLiteral("-");
    return QStringLiteral("%1:%2")
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch());
}

// One line of context around a hit, whitespace-collapsed so a snippet lifted out
// of a JSON event line still reads as a sentence in a tooltip.
QString hitSnippet(const QString &text, int at, int length)
{
    constexpr int kContext = 60;
    const qsizetype from = std::max<qsizetype>(0, at - kContext);
    const qsizetype to =
        std::min<qsizetype>(text.size(), at + length + kContext);
    QString snippet = text.mid(from, to - from).simplified();
    if (from > 0)
        snippet.prepend(QString::fromUtf8("\xE2\x80\xA6"));
    if (to < text.size())
        snippet.append(QString::fromUtf8("\xE2\x80\xA6"));
    return snippet;
}

} // namespace

int AgentStore::searchTranscript(const AgentSession &session,
                                 const QString &needle, QString *snippet) const
{
    const QString query = needle.trimmed();
    if (query.isEmpty())
        return 0;
    const QString dir = sessionDir(session);
    const QStringList files{QStringLiteral("/transcript.txt"),
                            QStringLiteral("/events.jsonl")};
    int hits = 0;
    for (const QString &name : files) {
        const QString text =
            readTranscriptTail(dir + name, kTranscriptSearchTailBytes);
        int at = text.indexOf(query, 0, Qt::CaseInsensitive);
        while (at >= 0) {
            if (snippet && snippet->isEmpty())
                *snippet = hitSnippet(text, at, query.size());
            ++hits;
            at = text.indexOf(query, at + query.size(), Qt::CaseInsensitive);
        }
    }
    return hits;
}

QStringList AgentStore::attachmentPathsIn(const QString &text)
{
    if (!text.contains(QLatin1String("Attached image:")))
        return {}; // the common case: no regex pass over a whole transcript
    // The path runs to the end of its line. The same line is read back both as
    // plain prompt text and out of a JSON-encoded transcript event, where the
    // line ends at the closing quote or at an escape ("\n") rather than at a real
    // newline — so a quote and a backslash end the path too.
    static const QRegularExpression marker(
        QStringLiteral("Attached image:[ \\t]*([^\"\\\\\\r\\n]+)"));
    QStringList paths;
    QRegularExpressionMatchIterator it = marker.globalMatch(text);
    while (it.hasNext()) {
        const QString path = it.next().captured(1).trimmed();
        if (!path.isEmpty() && !paths.contains(path))
            paths.append(path);
    }
    return paths;
}

QStringList AgentStore::attachmentPaths(const AgentSession &session) const
{
    const QString dir = sessionDir(session);
    QStringList paths = attachmentPathsIn(session.prompt);
    for (const QString &name : {QStringLiteral("/transcript.txt"),
                                QStringLiteral("/events.jsonl")}) {
        const QStringList found = attachmentPathsIn(
            readTranscriptEnds(dir + name, kAttachmentScanBytes));
        for (const QString &path : found)
            if (!paths.contains(path))
                paths.append(path);
    }
    return paths;
}

QString AgentStore::transcriptStamp(const AgentSession &session) const
{
    const QString dir = sessionDir(session);
    return fileStamp(dir + QStringLiteral("/transcript.txt")) +
           QLatin1Char('|') + fileStamp(dir + QStringLiteral("/events.jsonl"));
}

QList<AgentSession> AgentStore::loadAllSessions() const
{
    QList<AgentSession> sessions;
    QDir root(sessionsDir());
    const QStringList repos =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &repo : repos) {
        QDir repoDir(root.filePath(repo));
        const QStringList ids =
            repoDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &id : ids) {
            QFile file(repoDir.filePath(id) + QStringLiteral("/meta.json"));
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject())
                sessions.append(AgentSession::fromJson(doc.object()));
        }
    }
    const auto updatedAtMs = [](const AgentSession &session) {
        return qMax(qMax(session.createdAtMs, session.startedAtMs),
                    qMax(session.finishedAtMs, session.mergedAtMs));
    };
    std::sort(sessions.begin(), sessions.end(),
              [updatedAtMs](const AgentSession &a, const AgentSession &b) {
                  const qint64 aUpdated = updatedAtMs(a);
                  const qint64 bUpdated = updatedAtMs(b);
                  if (aUpdated != bUpdated)
                      return aUpdated > bUpdated;
                  return a.id > b.id;
              });
    return sessions;
}

QList<int> AgentStore::queuedSessionIdsOldestFirst(
    const QList<AgentSession> &sessions)
{
    QList<const AgentSession *> queued;
    for (const AgentSession &session : sessions) {
        if (session.status == AgentStatus::Queued)
            queued.append(&session);
    }
    std::sort(queued.begin(), queued.end(),
              [](const AgentSession *a, const AgentSession *b) {
                  if (a->createdAtMs != b->createdAtMs)
                      return a->createdAtMs < b->createdAtMs;
                  return a->id < b->id;
              });
    QList<int> ids;
    ids.reserve(queued.size());
    for (const AgentSession *session : std::as_const(queued))
        ids.append(session->id);
    return ids;
}
