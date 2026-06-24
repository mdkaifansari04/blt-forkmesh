#include "RepoSecurity.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

constexpr qsizetype kMaxScannedFileBytes = 1024 * 1024;

struct RepoFile {
    QString path;
    QByteArray content;
};

QString repoKey(const RepoSecurityInput &input)
{
    return input.owner + QStringLiteral("/") + input.name;
}

QString runGit(const QString &dir, const QStringList &args)
{
    if (dir.trimmed().isEmpty())
        return QString();
    QProcess git;
    git.start(QStringLiteral("git"), QStringList{QStringLiteral("-C"), dir} + args);
    if (!git.waitForFinished(8000) || git.exitCode() != 0)
        return QString();
    return QString::fromUtf8(git.readAllStandardOutput()).trimmed();
}

QString currentRefFor(const RepoSecurityInput &input)
{
    if (!input.ref.trimmed().isEmpty())
        return input.ref.trimmed();
    QString ref = runGit(input.localPath, {QStringLiteral("rev-parse"),
                                           QStringLiteral("--short"), QStringLiteral("HEAD")});
    if (!ref.isEmpty())
        return ref;
    ref = runGit(input.mirrorPath, {QStringLiteral("rev-parse"),
                                    QStringLiteral("--short"), QStringLiteral("HEAD")});
    return ref.isEmpty() ? QStringLiteral("HEAD") : ref;
}

QStringList trackedPathsFromLocal(const QString &localPath)
{
    const QString out = runGit(localPath, {QStringLiteral("ls-files")});
    return out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QStringList trackedPathsFromMirror(const QString &mirrorPath)
{
    const QString out = runGit(mirrorPath, {QStringLiteral("ls-tree"),
                                            QStringLiteral("-r"),
                                            QStringLiteral("--name-only"),
                                            QStringLiteral("HEAD")});
    return out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QByteArray readLocalFile(const QString &localPath, const QString &relPath)
{
    QFile file(QDir(localPath).filePath(relPath));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    if (file.size() > kMaxScannedFileBytes)
        return {};
    return file.readAll();
}

QByteArray readMirrorFile(const QString &mirrorPath, const QString &relPath)
{
    QProcess git;
    git.start(QStringLiteral("git"),
              {QStringLiteral("-C"), mirrorPath, QStringLiteral("show"),
               QStringLiteral("HEAD:") + relPath});
    if (!git.waitForFinished(8000) || git.exitCode() != 0)
        return {};
    const QByteArray content = git.readAllStandardOutput();
    if (content.size() > kMaxScannedFileBytes)
        return {};
    return content;
}

bool isLikelyText(const QByteArray &content)
{
    if (content.isEmpty())
        return true;
    return !content.contains('\0');
}

QList<RepoFile> trackedTextFiles(const RepoSecurityInput &input)
{
    QList<RepoFile> files;
    const bool useLocal = !input.localPath.trimmed().isEmpty() &&
                          QDir(input.localPath).exists();
    const bool useMirror = !useLocal && !input.mirrorPath.trimmed().isEmpty() &&
                           QDir(input.mirrorPath).exists();
    const QStringList paths = useLocal ? trackedPathsFromLocal(input.localPath)
                                       : trackedPathsFromMirror(input.mirrorPath);
    for (const QString &path : paths) {
        QByteArray content = useLocal ? readLocalFile(input.localPath, path)
                                      : useMirror ? readMirrorFile(input.mirrorPath, path)
                                                  : QByteArray();
        if (content.size() > kMaxScannedFileBytes || !isLikelyText(content))
            continue;
        files.append({path, content});
    }
    return files;
}

bool pathEqualsAny(const QString &path, const QSet<QString> &needles)
{
    const QString normalized = path.trimmed().toLower();
    return needles.contains(normalized);
}

QStringList dependencyManifests(const QList<RepoFile> &files)
{
    const QSet<QString> names{
        QStringLiteral("package.json"),
        QStringLiteral("package-lock.json"),
        QStringLiteral("pnpm-lock.yaml"),
        QStringLiteral("yarn.lock"),
        QStringLiteral("requirements.txt"),
        QStringLiteral("pyproject.toml"),
        QStringLiteral("pipfile"),
        QStringLiteral("pipfile.lock"),
        QStringLiteral("pubspec.yaml"),
        QStringLiteral("pubspec.lock"),
        QStringLiteral("cargo.toml"),
        QStringLiteral("cargo.lock"),
        QStringLiteral("go.mod"),
        QStringLiteral("go.sum"),
        QStringLiteral("pom.xml"),
        QStringLiteral("build.gradle"),
        QStringLiteral("build.gradle.kts"),
        QStringLiteral("gemfile"),
        QStringLiteral("gemfile.lock"),
        QStringLiteral("composer.json"),
        QStringLiteral("composer.lock"),
        QStringLiteral("mix.exs"),
        QStringLiteral("deno.json"),
    };

    QStringList found;
    for (const RepoFile &file : files) {
        const QString base = QFileInfo(file.path).fileName().toLower();
        if (names.contains(base))
            found << file.path;
    }
    found.removeDuplicates();
    std::sort(found.begin(), found.end());
    return found;
}

int lineNumberForOffset(const QByteArray &content, qsizetype offset)
{
    int line = 1;
    const qsizetype capped = qMin(offset, content.size());
    for (qsizetype i = 0; i < capped; ++i)
        if (content.at(i) == '\n')
            ++line;
    return line;
}

QString redacted(QString match)
{
    match = match.trimmed();
    if (match.size() <= 10)
        return QStringLiteral("[redacted]");
    return match.left(4) + QStringLiteral("...") + match.right(4);
}

QList<RepoSecurityFinding> secretFindings(const QList<RepoFile> &files)
{
    struct Pattern {
        QString name;
        QRegularExpression re;
    };
    const QList<Pattern> patterns{
        {QStringLiteral("GitHub token"),
         QRegularExpression(QStringLiteral("\\bgh[pousr]_[A-Za-z0-9_]{36}\\b"))},
        {QStringLiteral("AWS access key"),
         QRegularExpression(QStringLiteral("\\bAKIA[0-9A-Z]{16}\\b"))},
        {QStringLiteral("Slack token"),
         QRegularExpression(QStringLiteral("\\bxox[baprs]-[A-Za-z0-9-]{20,}\\b"))},
        {QStringLiteral("Private key"),
         QRegularExpression(QStringLiteral("-----BEGIN (RSA |EC |OPENSSH |)PRIVATE KEY-----"))},
    };

    QList<RepoSecurityFinding> findings;
    for (const RepoFile &file : files) {
        const QString text = QString::fromUtf8(file.content);
        for (const Pattern &pattern : patterns) {
            auto matches = pattern.re.globalMatch(text);
            while (matches.hasNext()) {
                const QRegularExpressionMatch match = matches.next();
                RepoSecurityFinding finding;
                finding.id = QStringLiteral("secret:%1:%2")
                                 .arg(file.path)
                                 .arg(match.capturedStart());
                finding.category = QStringLiteral("Secret");
                finding.severity = RepoSecuritySeverity::Critical;
                finding.title = pattern.name;
                finding.detail = QStringLiteral("Possible %1 in tracked file: %2")
                                     .arg(pattern.name.toLower(),
                                          redacted(match.captured(0)));
                finding.path = file.path;
                finding.line = lineNumberForOffset(file.content, match.capturedStart());
                finding.recommendedAction =
                    QStringLiteral("Rotate the credential and remove it from git.");
                findings.append(finding);
            }
        }
    }
    return findings;
}

bool hasSecurityPolicy(const QList<RepoFile> &files)
{
    const QSet<QString> policyPaths{
        QStringLiteral("security.md"),
        QStringLiteral(".github/security.md"),
        QStringLiteral("docs/security.md"),
    };
    for (const RepoFile &file : files)
        if (pathEqualsAny(file.path, policyPaths))
            return true;
    return false;
}

int openSecurityIssueCount(const QList<Issue> &issues)
{
    int count = 0;
    for (const Issue &issue : issues) {
        if (issue.status != QLatin1String("open"))
            continue;
        for (const QString &label : issue.labels)
            if (label.compare(QStringLiteral("security"), Qt::CaseInsensitive) == 0) {
                ++count;
                break;
            }
    }
    return count;
}

RepoSecuritySeverity qualitySeverity(const QList<ActionRun> &runs)
{
    bool hasRun = false;
    bool hasRunning = false;
    for (const ActionRun &run : runs) {
        hasRun = true;
        if (run.status == ActionStatus::Failed || run.status == ActionStatus::Rejected)
            return RepoSecuritySeverity::High;
        if (run.status == ActionStatus::Running || run.status == ActionStatus::Queued ||
            run.status == ActionStatus::AwaitingApproval)
            hasRunning = true;
    }
    if (!hasRun)
        return RepoSecuritySeverity::Warning;
    return hasRunning ? RepoSecuritySeverity::Info : RepoSecuritySeverity::Pass;
}

QString plural(int n, const QString &singular, const QString &plural)
{
    return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? singular : plural);
}

RepoSecuritySignal signal(QString key, QString title, RepoSecuritySeverity severity,
                          QString summary, QString detail = QString(),
                          QString actionLabel = QString(),
                          QString actionTarget = QString())
{
    RepoSecuritySignal out;
    out.key = std::move(key);
    out.title = std::move(title);
    out.severity = severity;
    out.summary = std::move(summary);
    out.detail = std::move(detail);
    out.actionLabel = std::move(actionLabel);
    out.actionTarget = std::move(actionTarget);
    return out;
}

} // namespace

RepoSecuritySnapshot RepoSecurity::scan(const RepoSecurityInput &input)
{
    RepoSecuritySnapshot snapshot;
    snapshot.repoKey = repoKey(input);
    snapshot.ref = currentRefFor(input);
    snapshot.generatedAtMs = QDateTime::currentMSecsSinceEpoch();

    const QList<RepoFile> files = trackedTextFiles(input);
    const bool policyPresent = hasSecurityPolicy(files);
    snapshot.signalList.append(
        policyPresent
            ? signal(QStringLiteral("policy"), QStringLiteral("Security policy"),
                     RepoSecuritySeverity::Pass,
                     QStringLiteral("SECURITY.md is present."),
                     QStringLiteral("ForkMesh found security reporting guidance."))
            : signal(QStringLiteral("policy"), QStringLiteral("Security policy"),
                     RepoSecuritySeverity::Warning,
                     QStringLiteral("No SECURITY.md found."),
                     QStringLiteral("Add reporting instructions before public launch."),
                     QStringLiteral("Create SECURITY.md"),
                     QStringLiteral("create:security-policy")));

    const QList<RepoSecurityFinding> secrets = secretFindings(files);
    snapshot.findings.append(secrets);
    snapshot.signalList.append(
        secrets.isEmpty()
            ? signal(QStringLiteral("secrets"), QStringLiteral("Secret scan"),
                     RepoSecuritySeverity::Pass,
                     QStringLiteral("No high-confidence secrets found."),
                     QStringLiteral("This checks tracked text files at the selected ref."))
            : signal(QStringLiteral("secrets"), QStringLiteral("Secret scan"),
                     RepoSecuritySeverity::Critical,
                     plural(secrets.size(), QStringLiteral("probable secret"),
                            QStringLiteral("probable secrets")) +
                         QStringLiteral(" found."),
                     QStringLiteral("Matched values are redacted. Rotate any exposed keys.")));

    const QStringList manifests = dependencyManifests(files);
    snapshot.signalList.append(
        manifests.isEmpty()
            ? signal(QStringLiteral("dependencies"),
                     QStringLiteral("Dependency inventory"),
                     RepoSecuritySeverity::Info,
                     QStringLiteral("No dependency manifests detected."),
                     QStringLiteral("MVP does not perform external advisory matching."))
            : signal(QStringLiteral("dependencies"),
                     QStringLiteral("Dependency inventory"),
                     RepoSecuritySeverity::Info,
                     plural(manifests.size(), QStringLiteral("manifest"),
                            QStringLiteral("manifests")) +
                         QStringLiteral(" detected."),
                     manifests.join(QStringLiteral(", "))));

    RepoSecuritySeverity actionsSeverity = RepoSecuritySeverity::Pass;
    QString actionsSummary;
    QString actionsDetail;
    if (!input.actionsEnabled) {
        actionsSeverity = RepoSecuritySeverity::Warning;
        actionsSummary = QStringLiteral("Actions are disabled.");
        actionsDetail = QStringLiteral("Workflows will not run on push.");
    } else if (input.workflows.isEmpty()) {
        actionsSeverity = RepoSecuritySeverity::Warning;
        actionsSummary = QStringLiteral("No .forkmesh workflows found.");
        actionsDetail = QStringLiteral("Add a workflow to make checks visible.");
    } else {
        int invalid = 0;
        for (const ActionWorkflow &workflow : input.workflows)
            if (!workflow.valid)
                ++invalid;
        if (invalid > 0) {
            actionsSeverity = RepoSecuritySeverity::High;
            actionsSummary = plural(invalid, QStringLiteral("invalid workflow"),
                                    QStringLiteral("invalid workflows"));
            actionsDetail = QStringLiteral("Fix workflow parsing errors.");
        } else {
            actionsSummary = plural(input.workflows.size(), QStringLiteral("workflow"),
                                    QStringLiteral("workflows")) +
                             QStringLiteral(" configured.");
            actionsDetail =
                QStringLiteral("Changed workflow content still uses existing approval.");
        }
    }
    snapshot.signalList.append(signal(QStringLiteral("actions"),
                                   QStringLiteral("Actions trust"),
                                   actionsSeverity, actionsSummary, actionsDetail,
                                   QStringLiteral("Open Actions"),
                                   QStringLiteral("tab:actions")));

    const RepoSecuritySeverity qSeverity = qualitySeverity(input.actionRuns);
    QString qualitySummary;
    if (qSeverity == RepoSecuritySeverity::High)
        qualitySummary = QStringLiteral("Latest quality checks need attention.");
    else if (qSeverity == RepoSecuritySeverity::Warning)
        qualitySummary = QStringLiteral("No quality check run recorded.");
    else if (qSeverity == RepoSecuritySeverity::Info)
        qualitySummary = QStringLiteral("Quality checks are queued or running.");
    else
        qualitySummary = QStringLiteral("Quality checks passed.");
    snapshot.signalList.append(signal(QStringLiteral("quality"),
                                   QStringLiteral("Quality checks"), qSeverity,
                                   qualitySummary,
                                   QStringLiteral("Derived from local ForkMesh action runs."),
                                   QStringLiteral("Open Actions"),
                                   QStringLiteral("tab:actions")));

    const int securityIssues = openSecurityIssueCount(input.issues);
    snapshot.signalList.append(
        securityIssues > 0
            ? signal(QStringLiteral("issues"), QStringLiteral("Security backlog"),
                     RepoSecuritySeverity::Warning,
                     plural(securityIssues, QStringLiteral("open security issue"),
                            QStringLiteral("open security issues")),
                     QStringLiteral("Issues tagged security should stay visible here."),
                     QStringLiteral("Open Issues"),
                     QStringLiteral("tab:issues:security"))
            : signal(QStringLiteral("issues"), QStringLiteral("Security backlog"),
                     RepoSecuritySeverity::Pass,
                     QStringLiteral("No open security-labelled issues."),
                     QStringLiteral("This checks signed issue metadata.")));

    RepoSecuritySeverity trustSeverity = RepoSecuritySeverity::Info;
    QString trustSummary;
    if (input.integrityWarning) {
        trustSeverity = RepoSecuritySeverity::Critical;
        trustSummary = QStringLiteral("Repository integrity pin needs attention.");
    } else if (input.previewOnly) {
        trustSeverity = RepoSecuritySeverity::Warning;
        trustSummary = QStringLiteral("Preview-only repository.");
    } else if (input.isPrivate) {
        trustSummary = QStringLiteral("Private repository.");
    } else if (input.publishToNetwork) {
        trustSummary = QStringLiteral("Published public repository.");
    } else {
        trustSummary = QStringLiteral("Local-only repository.");
    }
    snapshot.signalList.append(signal(QStringLiteral("trust"),
                                   QStringLiteral("ForkMesh trust"), trustSeverity,
                                   trustSummary,
                                   QStringLiteral("Derived from local repository metadata.")));

    return snapshot;
}

QString RepoSecurity::severityText(RepoSecuritySeverity severity)
{
    switch (severity) {
    case RepoSecuritySeverity::Pass:
        return QStringLiteral("Pass");
    case RepoSecuritySeverity::Info:
        return QStringLiteral("Info");
    case RepoSecuritySeverity::Warning:
        return QStringLiteral("Warning");
    case RepoSecuritySeverity::High:
        return QStringLiteral("High");
    case RepoSecuritySeverity::Critical:
        return QStringLiteral("Critical");
    }
    return QStringLiteral("Info");
}

RepoSecuritySeverity RepoSecurity::highestSeverity(
    const RepoSecuritySnapshot &snapshot)
{
    RepoSecuritySeverity highest = RepoSecuritySeverity::Pass;
    auto rank = [](RepoSecuritySeverity severity) {
        switch (severity) {
        case RepoSecuritySeverity::Pass:
            return 0;
        case RepoSecuritySeverity::Info:
            return 1;
        case RepoSecuritySeverity::Warning:
            return 2;
        case RepoSecuritySeverity::High:
            return 3;
        case RepoSecuritySeverity::Critical:
            return 4;
        }
        return 1;
    };
    for (const RepoSecuritySignal &signal : snapshot.signalList)
        if (rank(signal.severity) > rank(highest))
            highest = signal.severity;
    for (const RepoSecurityFinding &finding : snapshot.findings)
        if (rank(finding.severity) > rank(highest))
            highest = finding.severity;
    return highest;
}
