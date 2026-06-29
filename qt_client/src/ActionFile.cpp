#include "ActionFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

// A parsed YAML value: scalar, mapping, or sequence. Deliberately minimal.
struct Node {
    enum Type { Null, Scalar, Map, Seq };
    Type type = Null;
    QString scalar;
    QMap<QString, Node> map;
    QList<Node> seq;

    const Node *child(const QString &key) const
    {
        auto it = map.constFind(key);
        return it == map.constEnd() ? nullptr : &it.value();
    }
};

struct Line {
    int indent;
    QString text; // content with indentation stripped, never empty
};

int leadingSpaces(const QString &raw)
{
    int n = 0;
    while (n < raw.size() && raw.at(n) == QLatin1Char(' '))
        ++n;
    return n;
}

// Split into significant lines, expanding tabs to spaces and dropping blank and
// full-line comment lines. Block scalars are handled later from the raw text, so
// here we keep enough structure to recurse by indentation.
QList<Line> splitLines(const QString &content)
{
    QList<Line> out;
    const QStringList raw = content.split(QLatin1Char('\n'));
    for (QString line : raw) {
        line.replace(QLatin1Char('\t'), QStringLiteral("    "));
        // Drop trailing carriage returns from CRLF files.
        while (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        Line l;
        l.indent = leadingSpaces(line);
        l.text = line.mid(l.indent);
        out.append(l);
    }
    return out;
}

QString unquote(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 &&
        ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) ||
         (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

// Parse an inline flow sequence: [a, b, "c"]  -> Seq of scalars.
Node parseFlowSeq(const QString &inner)
{
    Node node;
    node.type = Node::Seq;
    const QStringList parts = inner.split(QLatin1Char(','));
    for (const QString &part : parts) {
        const QString v = unquote(part);
        if (v.isEmpty())
            continue;
        Node item;
        item.type = Node::Scalar;
        item.scalar = v;
        node.seq.append(item);
    }
    return node;
}

// Capture a literal block scalar (introduced by `|`): every following line more
// indented than parentIndent, with the block's common indentation removed.
QString captureBlockScalar(const QList<Line> &lines, int &pos, int parentIndent)
{
    QStringList block;
    int blockIndent = -1;
    while (pos < lines.size() && lines.at(pos).indent > parentIndent) {
        const Line &l = lines.at(pos);
        if (blockIndent < 0)
            blockIndent = l.indent;
        const int strip = qMin(blockIndent, l.indent);
        block.append(QString(l.indent - strip, QLatin1Char(' ')) + l.text);
        ++pos;
    }
    return block.join(QLatin1Char('\n'));
}

// Forward declaration: parse a block (map or sequence) whose items are indented
// at exactly `indent`. Advances pos past the block.
Node parseBlock(const QList<Line> &lines, int &pos, int indent);

// Parse the value that follows a "key:" with nothing (or a comment) after the
// colon — i.e. a nested block on the following lines.
Node parseNestedValue(const QList<Line> &lines, int &pos, int parentIndent)
{
    if (pos >= lines.size() || lines.at(pos).indent <= parentIndent) {
        Node empty;
        return empty; // Null
    }
    return parseBlock(lines, pos, lines.at(pos).indent);
}

// Parse "key: rest" / "key:" and store into map. Handles inline scalar, flow
// sequence, block scalar marker, and nested blocks.
void parseMapEntry(const QList<Line> &lines, int &pos, int indent, Node &map)
{
    const QString text = lines.at(pos).text;
    const int colon = text.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        ++pos; // not a key: skip defensively
        return;
    }
    const QString key = unquote(text.left(colon));
    QString rest = text.mid(colon + 1).trimmed();
    ++pos;

    Node value;
    if (rest == QLatin1String("|") || rest == QLatin1String("|-") ||
        rest == QLatin1String(">") || rest == QLatin1String(">-")) {
        value.type = Node::Scalar;
        value.scalar = captureBlockScalar(lines, pos, indent);
    } else if (rest.startsWith(QLatin1Char('[')) && rest.endsWith(QLatin1Char(']'))) {
        value = parseFlowSeq(rest.mid(1, rest.size() - 2));
    } else if (!rest.isEmpty()) {
        value.type = Node::Scalar;
        value.scalar = unquote(rest);
    } else {
        value = parseNestedValue(lines, pos, indent);
    }
    map.map.insert(key, value);
}

Node parseBlock(const QList<Line> &lines, int &pos, int indent)
{
    Node node;
    const bool isSeq = lines.at(pos).text.startsWith(QLatin1String("- ")) ||
                       lines.at(pos).text == QLatin1String("-");
    node.type = isSeq ? Node::Seq : Node::Map;

    while (pos < lines.size() && lines.at(pos).indent == indent) {
        const Line &line = lines.at(pos);
        if (node.type == Node::Seq) {
            if (!line.text.startsWith(QLatin1Char('-')))
                break;
            const QString after = line.text.mid(1).trimmed();
            if (after.contains(QLatin1Char(':')) &&
                !after.startsWith(QLatin1Char('['))) {
                // "- key: value" introduces a map whose first entry is inline.
                // Re-feed the remainder at a virtual indent past the dash.
                const int dashCols = line.text.indexOf(after.at(0));
                QList<Line> synthetic = lines;
                Line first;
                first.indent = indent + dashCols;
                first.text = after;
                synthetic[pos] = first;
                int sub = pos;
                Node item = parseBlock(synthetic, sub, first.indent);
                node.seq.append(item);
                pos = sub;
            } else if (!after.isEmpty()) {
                Node item;
                item.type = Node::Scalar;
                item.scalar = unquote(after);
                node.seq.append(item);
                ++pos;
            } else {
                ++pos;
                node.seq.append(parseNestedValue(lines, pos, indent));
            }
        } else {
            parseMapEntry(lines, pos, indent, node);
        }
    }
    return node;
}

QStringList scalarOrList(const Node *node)
{
    QStringList out;
    if (!node)
        return out;
    if (node->type == Node::Scalar)
        out.append(node->scalar.trimmed());
    else if (node->type == Node::Seq)
        for (const Node &item : node->seq)
            if (item.type == Node::Scalar)
                out.append(item.scalar.trimmed());
    return out;
}

void collectSteps(const Node *stepsNode, QList<ActionStep> &steps)
{
    if (!stepsNode || stepsNode->type != Node::Seq)
        return;
    for (const Node &item : stepsNode->seq) {
        if (item.type != Node::Map)
            continue;
        ActionStep step;
        if (const Node *n = item.child(QStringLiteral("name")))
            step.name = n->scalar.trimmed();
        if (const Node *r = item.child(QStringLiteral("run")))
            step.run = r->scalar;
        if (!step.run.isEmpty())
            steps.append(step);
    }
}

} // namespace

ActionWorkflow ActionFile::parse(const QString &relPath, const QString &content)
{
    ActionWorkflow wf;
    wf.path = relPath;
    wf.content = content;
    wf.name = QFileInfo(relPath).fileName();

    QList<Line> lines = splitLines(content);
    if (lines.isEmpty()) {
        wf.error = QStringLiteral("empty workflow file");
        return wf;
    }

    int pos = 0;
    Node root = parseBlock(lines, pos, lines.first().indent);
    if (root.type != Node::Map) {
        wf.error = QStringLiteral("workflow root must be a mapping");
        return wf;
    }

    if (const Node *n = root.child(QStringLiteral("name")))
        if (!n->scalar.trimmed().isEmpty())
            wf.name = n->scalar.trimmed();

    wf.on = scalarOrList(root.child(QStringLiteral("on")));

    if (const Node *env = root.child(QStringLiteral("env")))
        if (env->type == Node::Map)
            for (auto it = env->map.constBegin(); it != env->map.constEnd(); ++it)
                wf.env.insert(it.key(), it.value().scalar);

    // Flattened top-level steps, then steps nested under each job.
    collectSteps(root.child(QStringLiteral("steps")), wf.steps);
    if (const Node *jobs = root.child(QStringLiteral("jobs")))
        if (jobs->type == Node::Map)
            for (auto it = jobs->map.constBegin(); it != jobs->map.constEnd(); ++it)
                collectSteps(it.value().child(QStringLiteral("steps")), wf.steps);

    if (wf.steps.isEmpty()) {
        wf.error = QStringLiteral("workflow has no runnable steps");
        return wf;
    }
    wf.valid = true;
    return wf;
}

QList<ActionWorkflow> ActionFile::parseWorkflowsInDir(const QString &checkoutDir)
{
    QList<ActionWorkflow> out;
    QDir dir(checkoutDir + QStringLiteral("/.forkmesh"));
    if (!dir.exists())
        return out;
    const QStringList files =
        dir.entryList({QStringLiteral("*.yml"), QStringLiteral("*.yaml")},
                      QDir::Files, QDir::Name);
    for (const QString &file : files) {
        QFile f(dir.filePath(file));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString content = QString::fromUtf8(f.readAll());
        out.append(parse(QStringLiteral(".forkmesh/") + file, content));
    }
    return out;
}

QString ActionFile::substitute(const QString &input,
                               const QMap<QString, QString> &vars)
{
    QString result = input;
    // ${{ vars.NAME }} with flexible whitespace — the explicit ForkMesh/CI
    // context reference, always expanded (empty for unknown names, matching CI
    // conventions).
    static const QRegularExpression ctx(
        QStringLiteral("\\$\\{\\{\\s*vars\\.([A-Za-z_][A-Za-z0-9_]*)\\s*\\}\\}"));
    // ${NAME} — ambiguous with ordinary shell parameter expansion. Only expand
    // it when NAME is a declared variable; otherwise leave it verbatim so the
    // shell can substitute its own variables. (Expanding every ${NAME} here used
    // to blank out a workflow's own shell variables — e.g. release.yml building
    // "releases/${channel}/forkmesh-${os}-${arch}" into "releases//forkmesh--".)
    static const QRegularExpression brace(
        QStringLiteral("\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}"));
    struct Pass {
        const QRegularExpression &re;
        bool onlyKnown;
    };
    for (const Pass &pass : {Pass{ctx, false}, Pass{brace, true}}) {
        QString out;
        int last = 0;
        auto it = pass.re.globalMatch(result);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString name = m.captured(1);
            if (pass.onlyKnown && !vars.contains(name))
                continue; // leave shell variables for the shell
            out += result.mid(last, m.capturedStart() - last);
            out += vars.value(name);
            last = m.capturedEnd();
        }
        out += result.mid(last);
        result = out;
    }
    return result;
}
