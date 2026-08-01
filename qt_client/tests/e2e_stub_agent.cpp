












#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QTextStream>

namespace {


bool git(const QString &dir, const QStringList &args, QByteArray *out = nullptr)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    p.start(QStringLiteral("git"), args);
    if (!p.waitForFinished(30000) || p.exitStatus() != QProcess::NormalExit ||
        p.exitCode() != 0) {
        QTextStream(stderr) << "stub-agent: git " << args.join(' ') << " failed: "
                            << QString::fromUtf8(p.readAllStandardError()) << '\n';
        return false;
    }
    if (out)
        *out = p.readAllStandardOutput();
    return true;
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 4) {
        QTextStream(stderr)
            << "usage: forkmesh-e2e-stub-agent <worktree> <branch> <issueNumber>\n";
        return 2;
    }
    const QString worktree = args.at(1);
    const QString branch = args.at(2);
    const QString issueNumber = args.at(3);

    if (!QDir(worktree).exists()) {
        QTextStream(stderr) << "stub-agent: worktree does not exist: " << worktree
                            << '\n';
        return 2;
    }


    if (!git(worktree, {"config", "user.email", "agent@forkmesh.test"}) ||
        !git(worktree, {"config", "user.name", "ForkMesh E2E Agent"}))
        return 1;


    if (!git(worktree, {"checkout", "-B", branch}))
        return 1;



    const QString notePath = QDir(worktree).filePath(QStringLiteral("AGENT_FIX.md"));
    QFile note(notePath);
    if (!note.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream(stderr) << "stub-agent: cannot write " << notePath << '\n';
        return 1;
    }
    note.write(QStringLiteral("Resolves issue #%1 via the nightly mesh-loop agent.\n")
                   .arg(issueNumber)
                   .toUtf8());
    note.close();

    if (!git(worktree, {"add", "AGENT_FIX.md"}) ||
        !git(worktree,
             {"commit", "-m",
              QStringLiteral("Fix issue #%1: add agent note").arg(issueNumber)}))
        return 1;

    QByteArray sha;
    if (!git(worktree, {"rev-parse", "HEAD"}, &sha))
        return 1;
    QTextStream(stdout) << QString::fromUtf8(sha).trimmed() << '\n';
    return 0;
}
