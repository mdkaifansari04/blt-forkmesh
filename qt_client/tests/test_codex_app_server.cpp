#include "../src/CodexAppServerSession.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (condition)
        qInfo("PASS: %s", what);
    else {
        qCritical("FAIL: %s", what);
        ++failures;
    }
}

template <typename Predicate> bool pump(Predicate predicate, int timeoutMs = 4000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return true;
}

void writeBytes(QFile &output, const QByteArray &bytes)
{
    output.write(bytes);
    output.flush();
}

void writeMessage(QFile &output, const QJsonObject &message,
                  bool fragmented = false)
{
    const QByteArray line =
        QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (!fragmented) {
        writeBytes(output, line);
        return;
    }
    const int split = line.size() / 2;
    writeBytes(output, line.left(split));
    QThread::msleep(25);
    writeBytes(output, line.mid(split));
}

QJsonObject turn(const QString &id, const QString &status,
                 const QJsonArray &items = QJsonArray())
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("items"), items},
                       {QStringLiteral("itemsView"), QStringLiteral("full")},
                       {QStringLiteral("status"), status},
                       {QStringLiteral("error"), QJsonValue(QJsonValue::Null)},
                       {QStringLiteral("startedAt"), 1},
                       {QStringLiteral("completedAt"),
                        status == QStringLiteral("inProgress")
                            ? QJsonValue(QJsonValue::Null)
                            : QJsonValue(2)},
                       {QStringLiteral("durationMs"),
                        status == QStringLiteral("inProgress")
                            ? QJsonValue(QJsonValue::Null)
                            : QJsonValue(123)}};
}

void itemNotification(QFile &output, const QString &method,
                      const QJsonObject &item, const QString &turnId)
{
    QJsonObject params{{QStringLiteral("threadId"), QStringLiteral("thread-fresh")},
                       {QStringLiteral("turnId"), turnId},
                       {QStringLiteral("item"), item}};
    params.insert(method == QStringLiteral("item/started")
                      ? QStringLiteral("startedAtMs")
                      : QStringLiteral("completedAtMs"),
                  1000);
    writeMessage(output, QJsonObject{{QStringLiteral("method"), method},
                                     {QStringLiteral("params"), params}});
}

QJsonObject commandItem(const QString &status)
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("commandExecution")},
        {QStringLiteral("id"), QStringLiteral("cmd-1")},
        {QStringLiteral("command"), QStringLiteral("printf hello")},
        {QStringLiteral("cwd"), QStringLiteral("/tmp")},
        {QStringLiteral("status"), status},
        {QStringLiteral("commandActions"), QJsonArray()},
        {QStringLiteral("aggregatedOutput"),
         status == QStringLiteral("completed") ? QJsonValue(QStringLiteral("hello"))
                                                : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("exitCode"),
         status == QStringLiteral("completed") ? QJsonValue(0)
                                                : QJsonValue(QJsonValue::Null)}};
}

void sendToolLifecycle(QFile &output, const QJsonObject &started,
                       QJsonObject completed, const QString &turnId)
{
    itemNotification(output, QStringLiteral("item/started"), started, turnId);
    itemNotification(output, QStringLiteral("item/completed"), completed, turnId);
}

void emitFreshTurn(QFile &output)
{
    const QString turnId = QStringLiteral("turn-fresh");
    writeMessage(output,
                 QJsonObject{{QStringLiteral("method"), QStringLiteral("turn/started")},
                             {QStringLiteral("params"),
                              QJsonObject{{QStringLiteral("threadId"),
                                           QStringLiteral("thread-fresh")},
                                          {QStringLiteral("turn"),
                                           turn(turnId, QStringLiteral("inProgress"))}}}});
    writeMessage(output,
                 QJsonObject{{QStringLiteral("method"),
                              QStringLiteral("item/agentMessage/delta")},
                             {QStringLiteral("params"),
                              QJsonObject{{QStringLiteral("threadId"),
                                           QStringLiteral("thread-fresh")},
                                          {QStringLiteral("turnId"), turnId},
                                          {QStringLiteral("itemId"),
                                           QStringLiteral("agent-1")},
                                          {QStringLiteral("delta"),
                                           QStringLiteral("hello ")}}}});
    writeMessage(output,
                 QJsonObject{{QStringLiteral("method"),
                              QStringLiteral("item/reasoning/textDelta")},
                             {QStringLiteral("params"),
                              QJsonObject{{QStringLiteral("threadId"),
                                           QStringLiteral("thread-fresh")},
                                          {QStringLiteral("turnId"), turnId},
                                          {QStringLiteral("itemId"),
                                           QStringLiteral("reason-1")},
                                          {QStringLiteral("contentIndex"), 0},
                                          {QStringLiteral("delta"),
                                           QStringLiteral("thinking")}}}});

    itemNotification(output, QStringLiteral("item/started"),
                     commandItem(QStringLiteral("inProgress")), turnId);
    writeMessage(output,
                 QJsonObject{{QStringLiteral("method"),
                              QStringLiteral("item/commandExecution/outputDelta")},
                             {QStringLiteral("params"),
                              QJsonObject{{QStringLiteral("threadId"),
                                           QStringLiteral("thread-fresh")},
                                          {QStringLiteral("turnId"), turnId},
                                          {QStringLiteral("itemId"),
                                           QStringLiteral("cmd-1")},
                                          {QStringLiteral("delta"),
                                           QStringLiteral("hello")}}}});
    itemNotification(output, QStringLiteral("item/completed"),
                     commandItem(QStringLiteral("completed")), turnId);

    const QJsonArray changes{QJsonObject{{QStringLiteral("path"),
                                         QStringLiteral("src/a.cpp")},
                                        {QStringLiteral("kind"),
                                         QJsonObject{{QStringLiteral("type"),
                                                      QStringLiteral("update")}}}}};
    sendToolLifecycle(
        output,
        QJsonObject{{QStringLiteral("type"), QStringLiteral("fileChange")},
                    {QStringLiteral("id"), QStringLiteral("file-1")},
                    {QStringLiteral("changes"), changes},
                    {QStringLiteral("status"), QStringLiteral("inProgress")}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("fileChange")},
                    {QStringLiteral("id"), QStringLiteral("file-1")},
                    {QStringLiteral("changes"), changes},
                    {QStringLiteral("status"), QStringLiteral("completed")}},
        turnId);
    sendToolLifecycle(
        output,
        QJsonObject{{QStringLiteral("type"), QStringLiteral("mcpToolCall")},
                    {QStringLiteral("id"), QStringLiteral("mcp-1")},
                    {QStringLiteral("server"), QStringLiteral("docs")},
                    {QStringLiteral("tool"), QStringLiteral("search")},
                    {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("q"),
                                                               QStringLiteral("Qt")}}},
                    {QStringLiteral("status"), QStringLiteral("inProgress")}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("mcpToolCall")},
                    {QStringLiteral("id"), QStringLiteral("mcp-1")},
                    {QStringLiteral("server"), QStringLiteral("docs")},
                    {QStringLiteral("tool"), QStringLiteral("search")},
                    {QStringLiteral("arguments"), QJsonObject()},
                    {QStringLiteral("status"), QStringLiteral("completed")},
                    {QStringLiteral("result"), QJsonObject{{QStringLiteral("ok"), true}}},
                    {QStringLiteral("error"), QJsonValue(QJsonValue::Null)}},
        turnId);
    sendToolLifecycle(
        output,
        QJsonObject{{QStringLiteral("type"), QStringLiteral("dynamicToolCall")},
                    {QStringLiteral("id"), QStringLiteral("dynamic-1")},
                    {QStringLiteral("tool"), QStringLiteral("custom_tool")},
                    {QStringLiteral("arguments"), QJsonObject()},
                    {QStringLiteral("status"), QStringLiteral("inProgress")}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("dynamicToolCall")},
                    {QStringLiteral("id"), QStringLiteral("dynamic-1")},
                    {QStringLiteral("tool"), QStringLiteral("custom_tool")},
                    {QStringLiteral("arguments"), QJsonObject()},
                    {QStringLiteral("status"), QStringLiteral("completed")},
                    {QStringLiteral("contentItems"), QJsonArray{QStringLiteral("done")}},
                    {QStringLiteral("success"), true}},
        turnId);
    sendToolLifecycle(
        output,
        QJsonObject{{QStringLiteral("type"), QStringLiteral("collabAgentToolCall")},
                    {QStringLiteral("id"), QStringLiteral("collab-1")},
                    {QStringLiteral("tool"), QStringLiteral("spawn_agent")},
                    {QStringLiteral("prompt"), QStringLiteral("inspect tests")},
                    {QStringLiteral("receiverThreadIds"),
                     QJsonArray{QStringLiteral("child-1")}},
                    {QStringLiteral("status"), QStringLiteral("inProgress")}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("collabAgentToolCall")},
                    {QStringLiteral("id"), QStringLiteral("collab-1")},
                    {QStringLiteral("tool"), QStringLiteral("spawn_agent")},
                    {QStringLiteral("receiverThreadIds"),
                     QJsonArray{QStringLiteral("child-1")}},
                    {QStringLiteral("agentsStates"),
                     QJsonObject{{QStringLiteral("child-1"),
                                  QStringLiteral("completed")}}},
                    {QStringLiteral("status"), QStringLiteral("completed")}},
        turnId);
    sendToolLifecycle(
        output,
        QJsonObject{{QStringLiteral("type"), QStringLiteral("webSearch")},
                    {QStringLiteral("id"), QStringLiteral("web-1")},
                    {QStringLiteral("query"), QStringLiteral("Qt JSONL")}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("webSearch")},
                    {QStringLiteral("id"), QStringLiteral("web-1")},
                    {QStringLiteral("query"), QStringLiteral("Qt JSONL")},
                    {QStringLiteral("action"),
                     QJsonObject{{QStringLiteral("type"),
                                  QStringLiteral("search")}}}},
        turnId);
    sendToolLifecycle(
        output,
        QJsonObject{{QStringLiteral("type"), QStringLiteral("imageView")},
                    {QStringLiteral("id"), QStringLiteral("image-view-1")},
                    {QStringLiteral("path"), QStringLiteral("/tmp/input.png")}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("imageView")},
                    {QStringLiteral("id"), QStringLiteral("image-view-1")},
                    {QStringLiteral("path"), QStringLiteral("/tmp/input.png")}},
        turnId);
    sendToolLifecycle(
        output,
        QJsonObject{{QStringLiteral("type"), QStringLiteral("imageGeneration")},
                    {QStringLiteral("id"), QStringLiteral("image-gen-1")},
                    {QStringLiteral("status"), QStringLiteral("inProgress")},
                    {QStringLiteral("revisedPrompt"), QStringLiteral("a diagram")}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("imageGeneration")},
                    {QStringLiteral("id"), QStringLiteral("image-gen-1")},
                    {QStringLiteral("status"), QStringLiteral("completed")},
                    {QStringLiteral("result"), QStringLiteral("image bytes")},
                    {QStringLiteral("savedPath"), QStringLiteral("/tmp/output.png")}},
        turnId);
    writeMessage(
        output,
        QJsonObject{
            {QStringLiteral("method"), QStringLiteral("turn/plan/updated")},
            {QStringLiteral("params"),
             QJsonObject{
                 {QStringLiteral("threadId"), QStringLiteral("thread-fresh")},
                 {QStringLiteral("turnId"), turnId},
                 {QStringLiteral("explanation"), QStringLiteral("Test the transport")},
                 {QStringLiteral("plan"),
                  QJsonArray{
                      QJsonObject{{QStringLiteral("step"),
                                   QStringLiteral("Inspect")},
                                  {QStringLiteral("status"),
                                   QStringLiteral("completed")}},
                      QJsonObject{{QStringLiteral("step"), QStringLiteral("Fix")},
                                  {QStringLiteral("status"),
                                   QStringLiteral("inProgress")}}}}}}});
    sendToolLifecycle(
        output,
        QJsonObject{{QStringLiteral("type"),
                     QStringLiteral("subAgentActivity")},
                    {QStringLiteral("id"), QStringLiteral("subactivity-1")},
                    {QStringLiteral("kind"), QStringLiteral("spawned")},
                    {QStringLiteral("agentThreadId"), QStringLiteral("child-2")},
                    {QStringLiteral("agentPath"), QStringLiteral("root/child")}},
        QJsonObject{{QStringLiteral("type"),
                     QStringLiteral("subAgentActivity")},
                    {QStringLiteral("id"), QStringLiteral("subactivity-1")},
                    {QStringLiteral("kind"), QStringLiteral("completed")},
                    {QStringLiteral("agentThreadId"), QStringLiteral("child-2")},
                    {QStringLiteral("agentPath"), QStringLiteral("root/child")}},
        turnId);
    sendToolLifecycle(
        output,
        QJsonObject{{QStringLiteral("type"), QStringLiteral("sleep")},
                    {QStringLiteral("id"), QStringLiteral("sleep-1")},
                    {QStringLiteral("durationMs"), 250}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("sleep")},
                    {QStringLiteral("id"), QStringLiteral("sleep-1")},
                    {QStringLiteral("durationMs"), 250}},
        turnId);
    for (const QJsonObject &special : {
             QJsonObject{{QStringLiteral("type"),
                          QStringLiteral("enteredReviewMode")},
                         {QStringLiteral("id"), QStringLiteral("review-in")},
                         {QStringLiteral("review"), QStringLiteral("Review changes")}},
             QJsonObject{{QStringLiteral("type"),
                          QStringLiteral("exitedReviewMode")},
                         {QStringLiteral("id"), QStringLiteral("review-out")},
                         {QStringLiteral("review"), QStringLiteral("Review complete")}},
             QJsonObject{{QStringLiteral("type"),
                          QStringLiteral("contextCompaction")},
                         {QStringLiteral("id"), QStringLiteral("compact-1")}}}) {
        itemNotification(output, QStringLiteral("item/started"), special, turnId);
        itemNotification(output, QStringLiteral("item/completed"), special, turnId);
    }

    itemNotification(
        output, QStringLiteral("item/completed"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("reasoning")},
                    {QStringLiteral("id"), QStringLiteral("reason-1")},
                    {QStringLiteral("summary"),
                     QJsonArray{QStringLiteral("Checked protocol")}},
                    {QStringLiteral("content"), QJsonArray()}},
        turnId);
    itemNotification(
        output, QStringLiteral("item/completed"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("agentMessage")},
                    {QStringLiteral("id"), QStringLiteral("agent-1")},
                    {QStringLiteral("text"), QStringLiteral("hello world")}},
        turnId);
    writeMessage(
        output,
        QJsonObject{{QStringLiteral("method"),
                     QStringLiteral("thread/tokenUsage/updated")},
                    {QStringLiteral("params"),
                     QJsonObject{
                         {QStringLiteral("threadId"), QStringLiteral("thread-fresh")},
                         {QStringLiteral("turnId"), turnId},
                         {QStringLiteral("tokenUsage"),
                          QJsonObject{{QStringLiteral("total"),
                                       QJsonObject{{QStringLiteral("totalTokens"), 42},
                                                   {QStringLiteral("inputTokens"), 30},
                                                   {QStringLiteral("outputTokens"), 12}}}}}}}});
    writeMessage(
        output,
        QJsonObject{{QStringLiteral("method"),
                     QStringLiteral("account/rateLimits/updated")},
                    {QStringLiteral("params"),
                     QJsonObject{{QStringLiteral("rateLimits"),
                                  QJsonObject{{QStringLiteral("limitId"),
                                               QStringLiteral("codex")},
                                              {QStringLiteral("primary"),
                                               QJsonObject{{QStringLiteral("usedPercent"),
                                                            25}}}}}}}});
    writeMessage(output,
                 QJsonObject{{QStringLiteral("method"), QStringLiteral("warning")},
                             {QStringLiteral("params"),
                              QJsonObject{{QStringLiteral("threadId"),
                                           QStringLiteral("thread-fresh")},
                                          {QStringLiteral("message"),
                                           QStringLiteral("stub warning")}}}});

    writeMessage(
        output,
        QJsonObject{
            {QStringLiteral("method"),
             QStringLiteral("item/commandExecution/requestApproval")},
            {QStringLiteral("id"), QStringLiteral("approval-command")},
            {QStringLiteral("params"),
             QJsonObject{{QStringLiteral("threadId"), QStringLiteral("thread-fresh")},
                         {QStringLiteral("turnId"), turnId},
                         {QStringLiteral("itemId"), QStringLiteral("cmd-approval")},
                         {QStringLiteral("reason"), QStringLiteral("Needs network")},
                         {QStringLiteral("command"), QStringLiteral("curl example.com")},
                         {QStringLiteral("availableDecisions"),
                          QJsonArray{QStringLiteral("accept"),
                                     QStringLiteral("acceptForSession"),
                                     QStringLiteral("decline")}}}}});
    writeMessage(
        output,
        QJsonObject{
            {QStringLiteral("method"),
             QStringLiteral("item/fileChange/requestApproval")},
            {QStringLiteral("id"), QStringLiteral("approval-file")},
            {QStringLiteral("params"),
             QJsonObject{{QStringLiteral("threadId"), QStringLiteral("thread-fresh")},
                         {QStringLiteral("turnId"), turnId},
                         {QStringLiteral("itemId"), QStringLiteral("file-approval")},
                         {QStringLiteral("reason"),
                          QStringLiteral("Write outside workspace")}}}});
    const QJsonArray colorOptions{
        QJsonObject{{QStringLiteral("label"), QStringLiteral("Blue")},
                    {QStringLiteral("description"), QStringLiteral("Use blue")}},
        QJsonObject{{QStringLiteral("label"), QStringLiteral("Green")},
                    {QStringLiteral("description"), QStringLiteral("Use green")}}};
    const QJsonObject colorQuestion{
        {QStringLiteral("id"), QStringLiteral("color")},
        {QStringLiteral("header"), QStringLiteral("Color")},
        {QStringLiteral("question"), QStringLiteral("Pick a color")},
        {QStringLiteral("isOther"), false},
        {QStringLiteral("isSecret"), false},
        {QStringLiteral("options"), colorOptions}};
    const QJsonObject sizeQuestion{
        {QStringLiteral("id"), QStringLiteral("size")},
        {QStringLiteral("header"), QStringLiteral("Size")},
        {QStringLiteral("question"), QStringLiteral("Pick a size")},
        {QStringLiteral("isOther"), false},
        {QStringLiteral("isSecret"), false},
        {QStringLiteral("options"),
         QJsonArray{
             QJsonObject{{QStringLiteral("label"), QStringLiteral("Large")},
                         {QStringLiteral("description"),
                          QStringLiteral("Use large")}},
             QJsonObject{{QStringLiteral("label"), QStringLiteral("Small")},
                         {QStringLiteral("description"),
                          QStringLiteral("Use small")}}}}};
    const QJsonObject inputParams{
        {QStringLiteral("threadId"), QStringLiteral("thread-fresh")},
        {QStringLiteral("turnId"), turnId},
        {QStringLiteral("itemId"), QStringLiteral("question-1")},
        {QStringLiteral("questions"), QJsonArray{colorQuestion, sizeQuestion}}};
    writeMessage(output,
                 QJsonObject{{QStringLiteral("method"),
                              QStringLiteral("item/tool/requestUserInput")},
                             {QStringLiteral("id"),
                              QStringLiteral("request-input")},
                             {QStringLiteral("params"), inputParams}});
}

int runStub(const QStringList &arguments)
{
    if (arguments.size() < 4)
        return 2;
    const QString scenario = arguments.at(2);
    QFile input;
    QFile output;
    QFile errors;
    if (!input.open(stdin, QIODevice::ReadOnly) ||
        !output.open(stdout, QIODevice::WriteOnly) ||
        !errors.open(stderr, QIODevice::WriteOnly))
        return 3;
    QFile log(arguments.at(3));
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return 4;

    int serverResponses = 0;
    int turnStarts = 0;
    for (;;) {
        const QByteArray line = input.readLine();
        if (line.isEmpty())
            return 0;
        log.write(line);
        log.flush();
        const QJsonObject message = QJsonDocument::fromJson(line).object();
        const QString method = message.value(QStringLiteral("method")).toString();
        const QJsonValue id = message.value(QStringLiteral("id"));
        if (method == QStringLiteral("initialize")) {
            writeBytes(output, "this is not json\n");
            writeMessage(output,
                         QJsonObject{{QStringLiteral("id"), id},
                                     {QStringLiteral("result"),
                                      QJsonObject{{QStringLiteral("userAgent"),
                                                   QStringLiteral("stub")}}}},
                         true);
            writeBytes(errors, "stub diagnostic\n");
        } else if (method == QStringLiteral("model/list")) {
            writeMessage(
                output,
                QJsonObject{
                    {QStringLiteral("id"), id},
                    {QStringLiteral("result"),
                     QJsonObject{
                         {QStringLiteral("data"),
                          QJsonArray{QJsonObject{
                              {QStringLiteral("id"), QStringLiteral("gpt-test")},
                              {QStringLiteral("model"), QStringLiteral("gpt-test")},
                              {QStringLiteral("displayName"), QStringLiteral("GPT Test")},
                              {QStringLiteral("hidden"), false}}}},
                         {QStringLiteral("nextCursor"),
                          QJsonValue(QJsonValue::Null)}}}});
        } else if (method == QStringLiteral("thread/start") ||
                   method == QStringLiteral("thread/resume")) {
            if (scenario == QStringLiteral("resume-error") &&
                method == QStringLiteral("thread/resume")) {
                writeMessage(
                    output,
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("error"),
                                 QJsonObject{{QStringLiteral("code"), -32001},
                                             {QStringLiteral("message"),
                                              QStringLiteral("thread missing")}}}});
                continue;
            }
            const QString threadId = method == QStringLiteral("thread/resume")
                                         ? QStringLiteral("thread-resumed")
                                         : QStringLiteral("thread-fresh");
            writeMessage(
                output,
                QJsonObject{
                    {QStringLiteral("id"), id},
                    {QStringLiteral("result"),
                     QJsonObject{{QStringLiteral("thread"),
                                  QJsonObject{{QStringLiteral("id"), threadId}}},
                                 {QStringLiteral("model"),
                                  (scenario == QStringLiteral("resume") ||
                                   scenario == QStringLiteral("steer-reject"))
                                      ? QStringLiteral("gpt-plan")
                                      : QStringLiteral("gpt-test")}}}});
        } else if (method == QStringLiteral("turn/start")) {
            ++turnStarts;
            const QString turnId =
                scenario == QStringLiteral("steer-reject") && turnStarts > 1
                    ? QStringLiteral("turn-replayed")
                    : scenario == QStringLiteral("options")
                          ? QStringLiteral("turn-options-%1").arg(turnStarts)
                    : (scenario == QStringLiteral("resume") ||
                       scenario == QStringLiteral("steer-reject"))
                                       ? QStringLiteral("turn-resumed")
                                       : QStringLiteral("turn-fresh");
            writeMessage(output,
                         QJsonObject{{QStringLiteral("id"), id},
                                     {QStringLiteral("result"),
                                      QJsonObject{{QStringLiteral("turn"),
                                                   turn(turnId,
                                                        QStringLiteral("inProgress"))}}}});
            if (scenario == QStringLiteral("options")) {
                const QJsonObject agent{
                    {QStringLiteral("type"), QStringLiteral("agentMessage")},
                    {QStringLiteral("id"),
                     QStringLiteral("agent-options-%1").arg(turnStarts)},
                    {QStringLiteral("text"), QStringLiteral("options complete")}};
                writeMessage(
                    output,
                    QJsonObject{{QStringLiteral("method"),
                                 QStringLiteral("turn/started")},
                                {QStringLiteral("params"),
                                 QJsonObject{{QStringLiteral("threadId"),
                                              QStringLiteral("thread-fresh")},
                                             {QStringLiteral("turn"),
                                              turn(turnId,
                                                   QStringLiteral("inProgress"))}}}});
                writeMessage(
                    output,
                    QJsonObject{{QStringLiteral("method"),
                                 QStringLiteral("turn/completed")},
                                {QStringLiteral("params"),
                                 QJsonObject{{QStringLiteral("threadId"),
                                              QStringLiteral("thread-fresh")},
                                             {QStringLiteral("turn"),
                                              turn(turnId,
                                                   QStringLiteral("completed"),
                                                   QJsonArray{agent})}}}});
                if (turnStarts > 2)
                    return 0;
            } else if (scenario == QStringLiteral("fatal-error")) {
                writeMessage(
                    output,
                    QJsonObject{{QStringLiteral("method"),
                                 QStringLiteral("turn/started")},
                                {QStringLiteral("params"),
                                 QJsonObject{{QStringLiteral("threadId"),
                                              QStringLiteral("thread-fresh")},
                                             {QStringLiteral("turn"),
                                              turn(turnId,
                                                   QStringLiteral("inProgress"))}}}});
                writeMessage(
                    output,
                    QJsonObject{{QStringLiteral("method"), QStringLiteral("error")},
                                {QStringLiteral("params"),
                                 QJsonObject{{QStringLiteral("threadId"),
                                              QStringLiteral("thread-fresh")},
                                             {QStringLiteral("turnId"), turnId},
                                             {QStringLiteral("willRetry"), false},
                                             {QStringLiteral("error"),
                                              QJsonObject{{QStringLiteral("message"),
                                                           QStringLiteral("fatal turn")}}}}}});
                writeMessage(
                    output,
                    QJsonObject{{QStringLiteral("method"),
                                 QStringLiteral("turn/completed")},
                                {QStringLiteral("params"),
                                 QJsonObject{{QStringLiteral("threadId"),
                                              QStringLiteral("thread-fresh")},
                                             {QStringLiteral("turn"),
                                              turn(turnId,
                                                   QStringLiteral("failed"))}}}});
                return 0;
            } else if (scenario == QStringLiteral("steer-reject") &&
                       turnStarts > 1) {
                const QJsonObject agent{
                    {QStringLiteral("type"), QStringLiteral("agentMessage")},
                    {QStringLiteral("id"), QStringLiteral("agent-replayed")},
                    {QStringLiteral("text"), QStringLiteral("replayed")}};
                writeMessage(
                    output,
                    QJsonObject{{QStringLiteral("method"),
                                 QStringLiteral("turn/completed")},
                                {QStringLiteral("params"),
                                 QJsonObject{{QStringLiteral("threadId"),
                                              QStringLiteral("thread-resumed")},
                                             {QStringLiteral("turn"),
                                              turn(turnId,
                                                   QStringLiteral("completed"),
                                                   QJsonArray{agent})}}}});
                return 0;
            } else if (scenario == QStringLiteral("fresh")) {
                emitFreshTurn(output);
            } else if (scenario == QStringLiteral("resume") ||
                       scenario == QStringLiteral("steer-reject") ||
                       scenario == QStringLiteral("interrupt")) {
                writeMessage(
                    output,
                    QJsonObject{{QStringLiteral("method"),
                                 QStringLiteral("turn/started")},
                                {QStringLiteral("params"),
                                 QJsonObject{{QStringLiteral("threadId"),
                                              scenario == QStringLiteral("interrupt")
                                                  ? QStringLiteral("thread-fresh")
                                                  : QStringLiteral("thread-resumed")},
                                             {QStringLiteral("turn"),
                                              turn(turnId,
                                                   QStringLiteral("inProgress"))}}}});
            } else {
                const QString status = scenario == QStringLiteral("interrupted")
                                           ? QStringLiteral("interrupted")
                                           : QStringLiteral("completed");
                writeMessage(
                    output,
                    QJsonObject{{QStringLiteral("method"),
                                 QStringLiteral("turn/completed")},
                                {QStringLiteral("params"),
                                 QJsonObject{{QStringLiteral("threadId"),
                                             QStringLiteral("thread-fresh")},
                                             {QStringLiteral("turn"),
                                              turn(turnId, status)}}}});
                return 0;
            }
        } else if (method == QStringLiteral("turn/interrupt")) {
            writeMessage(output,
                         QJsonObject{{QStringLiteral("id"), id},
                                     {QStringLiteral("result"), QJsonObject()}});
            writeMessage(
                output,
                QJsonObject{{QStringLiteral("method"),
                             QStringLiteral("turn/completed")},
                            {QStringLiteral("params"),
                             QJsonObject{{QStringLiteral("threadId"),
                                          QStringLiteral("thread-fresh")},
                                         {QStringLiteral("turn"),
                                          turn(QStringLiteral("turn-fresh"),
                                               QStringLiteral("interrupted"))}}}});
            return 0;
        } else if (method == QStringLiteral("turn/steer")) {
            if (scenario == QStringLiteral("steer-reject")) {
                writeMessage(
                    output,
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("error"),
                                 QJsonObject{{QStringLiteral("code"), -32002},
                                             {QStringLiteral("message"),
                                              QStringLiteral("turn not steerable")}}}});
                writeMessage(
                    output,
                    QJsonObject{{QStringLiteral("method"),
                                 QStringLiteral("turn/completed")},
                                {QStringLiteral("params"),
                                 QJsonObject{{QStringLiteral("threadId"),
                                              QStringLiteral("thread-resumed")},
                                             {QStringLiteral("turn"),
                                              turn(QStringLiteral("turn-resumed"),
                                                   QStringLiteral("interrupted"))}}}});
                continue;
            }
            writeMessage(output,
                         QJsonObject{{QStringLiteral("id"), id},
                                     {QStringLiteral("result"),
                                      QJsonObject{{QStringLiteral("turnId"),
                                                   QStringLiteral("turn-resumed")}}}});
            const QJsonObject agent{
                {QStringLiteral("type"), QStringLiteral("agentMessage")},
                {QStringLiteral("id"), QStringLiteral("agent-resumed")},
                {QStringLiteral("text"), QStringLiteral("steered")}};
            writeMessage(
                output,
                QJsonObject{{QStringLiteral("method"),
                             QStringLiteral("item/completed")},
                            {QStringLiteral("params"),
                             QJsonObject{{QStringLiteral("threadId"),
                                          QStringLiteral("thread-resumed")},
                                         {QStringLiteral("turnId"),
                                          QStringLiteral("turn-resumed")},
                                         {QStringLiteral("item"), agent},
                                         {QStringLiteral("completedAtMs"), 2}}}});
            writeMessage(
                output,
                QJsonObject{{QStringLiteral("method"),
                             QStringLiteral("turn/completed")},
                            {QStringLiteral("params"),
                             QJsonObject{{QStringLiteral("threadId"),
                                          QStringLiteral("thread-resumed")},
                                         {QStringLiteral("turn"),
                                          turn(QStringLiteral("turn-resumed"),
                                               QStringLiteral("completed"),
                                               QJsonArray{agent})}}}});
            return 0;
        } else if (method.isEmpty() && message.contains(QStringLiteral("id"))) {
            const QString responseId = id.toString();
            if (responseId == QStringLiteral("approval-command") ||
                responseId == QStringLiteral("approval-file") ||
                responseId == QStringLiteral("request-input"))
                ++serverResponses;
            if (serverResponses == 3) {
                const QJsonObject agent{
                    {QStringLiteral("type"), QStringLiteral("agentMessage")},
                    {QStringLiteral("id"), QStringLiteral("agent-1")},
                    {QStringLiteral("text"), QStringLiteral("hello world")}};
                writeMessage(
                    output,
                    QJsonObject{{QStringLiteral("method"),
                                 QStringLiteral("turn/completed")},
                                {QStringLiteral("params"),
                                 QJsonObject{{QStringLiteral("threadId"),
                                              QStringLiteral("thread-fresh")},
                                             {QStringLiteral("turn"),
                                              turn(QStringLiteral("turn-fresh"),
                                                   QStringLiteral("completed"),
                                                   QJsonArray{agent})}}}});
                return 0;
            }
        }
    }
}

QList<QJsonObject> readMessages(const QString &path)
{
    QList<QJsonObject> messages;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return messages;
    while (!file.atEnd()) {
        const QJsonDocument document = QJsonDocument::fromJson(file.readLine());
        if (document.isObject())
            messages.append(document.object());
    }
    return messages;
}

int methodIndex(const QList<QJsonObject> &messages, const QString &method)
{
    for (int i = 0; i < messages.size(); ++i) {
        if (messages.at(i).value(QStringLiteral("method")).toString() == method)
            return i;
    }
    return -1;
}

QJsonObject firstMethod(const QList<QJsonObject> &messages,
                        const QString &method)
{
    const int index = methodIndex(messages, method);
    return index >= 0 ? messages.at(index) : QJsonObject();
}

QJsonObject responseWithId(const QList<QJsonObject> &messages, const QString &id)
{
    for (const QJsonObject &message : messages) {
        if (message.value(QStringLiteral("id")).toString() == id &&
            message.value(QStringLiteral("method")).toString().isEmpty())
            return message;
    }
    return QJsonObject();
}

int eventCount(const QList<QJsonObject> &events, const QString &type)
{
    int count = 0;
    for (const QJsonObject &event : events) {
        if (event.value(QStringLiteral("type")).toString() == type)
            ++count;
    }
    return count;
}

QHash<QString, QString> questionTokens(const QList<QJsonObject> &events)
{
    QHash<QString, QString> tokens;
    for (const QJsonObject &event : events) {
        if (event.value(QStringLiteral("type")).toString() !=
            QStringLiteral("assistant"))
            continue;
        const QJsonArray blocks = event.value(QStringLiteral("message"))
                                      .toObject()
                                      .value(QStringLiteral("content"))
                                      .toArray();
        for (const QJsonValue &value : blocks) {
            const QJsonObject block = value.toObject();
            if (block.value(QStringLiteral("name")).toString() !=
                QStringLiteral("AskUserQuestion"))
                continue;
            const QJsonArray questions = block.value(QStringLiteral("input"))
                                             .toObject()
                                             .value(QStringLiteral("questions"))
                                             .toArray();
            if (!questions.isEmpty())
                tokens.insert(questions.first()
                                  .toObject()
                                  .value(QStringLiteral("header"))
                                  .toString(),
                              block.value(QStringLiteral("id")).toString());
        }
    }
    return tokens;
}

bool rawContainsMethod(const QStringList &lines, const QString &method)
{
    for (const QString &line : lines) {
        if (QJsonDocument::fromJson(line.toUtf8())
                .object()
                .value(QStringLiteral("method"))
                .toString() == method)
            return true;
    }
    return false;
}

void runFreshProtocolTest(const QString &executable)
{
    QTemporaryDir temp;
    check(temp.isValid(), "fresh protocol temp dir created");
    const QString logPath = temp.filePath(QStringLiteral("client.jsonl"));
    CodexAppServerSession session;
    session.setAppServerCommand(executable,
                                {QStringLiteral("--codex-stub"),
                                 QStringLiteral("fresh"), logPath});
    QList<QJsonObject> events;
    QStringList rawLines;
    QString stderrOutput;
    QJsonArray models;
    bool started = false;
    bool finished = false;
    int exitCode = -99;
    QObject::connect(&session, &CodexAppServerSession::started,
                     [&] { started = true; });
    QObject::connect(&session, &CodexAppServerSession::finished,
                     [&](int code) {
                         exitCode = code;
                         finished = true;
                     });
    QObject::connect(&session, &CodexAppServerSession::event,
                     [&](const QJsonObject &event) { events.append(event); });
    QObject::connect(&session, &CodexAppServerSession::rawLine,
                     [&](const QString &line) { rawLines.append(line); });
    QObject::connect(&session, &CodexAppServerSession::stderrText,
                     [&](const QString &text) { stderrOutput += text; });
    QObject::connect(&session, &CodexAppServerSession::modelsListed,
                     [&](const QJsonArray &listed) { models = listed; });

    const QString initialTask =
        QStringLiteral("Agent launch identity: provider display name: Codex; "
                       "resolved model: gpt-test; permission mode: Auto mode; "
                       "reasoning speed: high.\n\n"
                       "describe this\nAttached image: /tmp/example.png");
    session.start(temp.path(), {QStringLiteral("CODEX_STUB_ENV=present")}, initialTask,
                  QString(), QStringLiteral("gpt-test"), QStringLiteral("Auto mode"),
                  QStringLiteral("high"));
    check(pump([&] { return started; }), "app-server process starts");
    check(pump([&] { return questionTokens(events).size() == 3; }),
          "approval and user-input requests are normalized");

    const QHash<QString, QString> tokens = questionTokens(events);
    check(tokens.value(QStringLiteral("Command approval"))
              .contains(QStringLiteral("approval")),
          "approval question token is distinguishable from ordinary input");
    session.respondToRequest(tokens.value(QStringLiteral("Command approval")),
                             QStringLiteral("Allow once"));
    session.respondToRequest(tokens.value(QStringLiteral("File approval")),
                             QStringLiteral("Always allow"));
    session.respondToRequest(tokens.value(QStringLiteral("Color")),
                             QStringLiteral("Pick a color %1 Blue\n"
                                            "Pick a size %1 Large")
                                 .arg(QChar(0x2192)));
    check(pump([&] { return finished; }), "fresh stub completes after responses");
    check(exitCode == 0, "fresh stub exits successfully");
    check(!session.running(), "session reports stopped after child exit");
    check(session.threadId() == QStringLiteral("thread-fresh"),
          "thread id is retained");

    const QList<QJsonObject> sent = readMessages(logPath);
    const int initIndex = methodIndex(sent, QStringLiteral("initialize"));
    const int initializedIndex = methodIndex(sent, QStringLiteral("initialized"));
    const int modelsIndex = methodIndex(sent, QStringLiteral("model/list"));
    const int threadIndex = methodIndex(sent, QStringLiteral("thread/start"));
    const int turnIndex = methodIndex(sent, QStringLiteral("turn/start"));
    check(initIndex == 0, "initialize is the first protocol message");
    check(initializedIndex > initIndex && modelsIndex > initializedIndex &&
              threadIndex > modelsIndex && turnIndex > threadIndex,
          "initialized, model list, thread start, and turn start are ordered");
    const QJsonObject initialize = sent.at(initIndex);
    check(initialize.value(QStringLiteral("params"))
              .toObject()
              .value(QStringLiteral("capabilities"))
              .toObject()
              .value(QStringLiteral("experimentalApi"))
              .toBool(),
          "initialize enables experimentalApi");
    check(!initialize.contains(QStringLiteral("jsonrpc")),
          "Codex messages omit the jsonrpc wrapper");

    const QJsonObject threadParams = sent.at(threadIndex)
                                         .value(QStringLiteral("params"))
                                         .toObject();
    check(threadParams.value(QStringLiteral("approvalPolicy")).toString() ==
                  QStringLiteral("never") &&
              threadParams.value(QStringLiteral("sandbox")).toString() ==
                  QStringLiteral("workspace-write"),
          "Auto mode maps to never/workspace-write");
    const QJsonObject turnParams = sent.at(turnIndex)
                                       .value(QStringLiteral("params"))
                                       .toObject();
    const QJsonArray input = turnParams.value(QStringLiteral("input")).toArray();
    const QString expectedText =
        initialTask.left(initialTask.indexOf(QStringLiteral("\nAttached image:")));
    check(input.size() == 2 &&
              input.first().toObject().value(QStringLiteral("text")).toString() ==
                  expectedText &&
              input.last().toObject().value(QStringLiteral("type")).toString() ==
                  QStringLiteral("localImage") &&
              input.last().toObject().value(QStringLiteral("path")).toString() ==
                  QStringLiteral("/tmp/example.png"),
          "identity instruction and attached image reach turn/start");
    check(turnParams.value(QStringLiteral("model")).toString() ==
                  QStringLiteral("gpt-test") &&
              turnParams.value(QStringLiteral("effort")).toString() ==
                  QStringLiteral("high"),
          "model and reasoning effort reach turn/start");

    check(models.size() == 1 &&
              models.first().toObject().value(QStringLiteral("id")).toString() ==
                  QStringLiteral("gpt-test"),
          "model/list result is emitted");
    check(rawLines.contains(QStringLiteral("this is not json")),
          "malformed lines are preserved as raw output");
    check(rawLines.size() > 10, "fragmented JSON is reassembled into protocol lines");
    check(stderrOutput.contains(QStringLiteral("stub diagnostic")),
          "stderr is forwarded");

    check(eventCount(events, QStringLiteral("system")) == 1,
          "thread readiness emits one system/init event");
    check(eventCount(events, QStringLiteral("_codex_agent_delta")) == 1 &&
              eventCount(events, QStringLiteral("_codex_agent_complete")) == 1,
          "agent delta and completion events are normalized");
    check(eventCount(events, QStringLiteral("stream_event")) == 1 &&
              eventCount(events, QStringLiteral("_codex_reasoning_complete")) == 1,
          "reasoning stream and completion events are normalized");
    check(eventCount(events, QStringLiteral("_codex_tool_delta")) >= 1,
          "tool output deltas are normalized");
    QSet<QString> toolIds;
    QSet<QString> toolResultIds;
    for (const QJsonObject &event : events) {
        for (const QJsonValue &value : event.value(QStringLiteral("message"))
                                           .toObject()
                                           .value(QStringLiteral("content"))
                                           .toArray()) {
            const QJsonObject block = value.toObject();
            if (block.value(QStringLiteral("type")).toString() ==
                QStringLiteral("tool_use"))
                toolIds.insert(block.value(QStringLiteral("id")).toString());
            else if (block.value(QStringLiteral("type")).toString() ==
                     QStringLiteral("tool_result"))
                toolResultIds.insert(
                    block.value(QStringLiteral("tool_use_id")).toString());
        }
    }
    const QSet<QString> expectedToolIds{
        QStringLiteral("cmd-1"),        QStringLiteral("file-1"),
        QStringLiteral("mcp-1"),        QStringLiteral("dynamic-1"),
        QStringLiteral("collab-1"),     QStringLiteral("web-1"),
        QStringLiteral("image-view-1"), QStringLiteral("image-gen-1"),
        QStringLiteral("plan-turn-fresh"),
        QStringLiteral("subactivity-1"), QStringLiteral("sleep-1")};
    bool hasEveryToolUse = true;
    for (const QString &id : expectedToolIds)
        hasEveryToolUse = hasEveryToolUse && toolIds.contains(id);
    check(eventCount(events, QStringLiteral("assistant")) == 14 &&
              eventCount(events, QStringLiteral("user")) == 11 &&
              toolIds.size() == expectedToolIds.size() + 3 && hasEveryToolUse &&
              toolResultIds == expectedToolIds,
          "every Codex tool family emits one complete Claude-shaped lifecycle");
    check(eventCount(events, QStringLiteral("_codex_usage")) == 1,
          "token usage is normalized");
    check(eventCount(events, QStringLiteral("_codex_rate_limits")) == 1,
          "rate-limit updates use the Codex host event shape");
    check(eventCount(events, QStringLiteral("_local_notice")) == 4,
          "warnings, review mode, and compaction become deduplicated notices");
    check(eventCount(events, QStringLiteral("result")) == 1,
          "turn completion emits a result event");

    check(responseWithId(sent, QStringLiteral("approval-command"))
                  .value(QStringLiteral("result"))
                  .toObject()
                  .value(QStringLiteral("decision"))
                  .toString() == QStringLiteral("accept"),
          "command approval answer maps to accept");
    check(responseWithId(sent, QStringLiteral("approval-file"))
                  .value(QStringLiteral("result"))
                  .toObject()
                  .value(QStringLiteral("decision"))
                  .toString() == QStringLiteral("acceptForSession"),
          "file approval answer maps to acceptForSession");
    check(responseWithId(sent, QStringLiteral("request-input"))
                  .value(QStringLiteral("result"))
                  .toObject()
                  .value(QStringLiteral("answers"))
                  .toObject()
                  .value(QStringLiteral("color"))
                  .toObject()
                  .value(QStringLiteral("answers"))
                  .toArray()
                  .first()
                  .toString() == QStringLiteral("Blue"),
          "first requestUserInput answer maps by question id");
    check(responseWithId(sent, QStringLiteral("request-input"))
                  .value(QStringLiteral("result"))
                  .toObject()
                  .value(QStringLiteral("answers"))
                  .toObject()
                  .value(QStringLiteral("size"))
                  .toObject()
                  .value(QStringLiteral("answers"))
                  .toArray()
                  .first()
                  .toString() == QStringLiteral("Large"),
          "second requestUserInput answer maps from displayed question text");
}

void runResumeAndSteerTest(const QString &executable)
{
    QTemporaryDir temp;
    check(temp.isValid(), "resume protocol temp dir created");
    const QString logPath = temp.filePath(QStringLiteral("resume.jsonl"));
    CodexAppServerSession session;
    session.setAppServerCommand(executable,
                                {QStringLiteral("--codex-stub"),
                                 QStringLiteral("resume"), logPath});
    QList<QJsonObject> events;
    QStringList rawLines;
    bool finished = false;
    QObject::connect(&session, &CodexAppServerSession::event,
                     [&](const QJsonObject &event) { events.append(event); });
    QObject::connect(&session, &CodexAppServerSession::rawLine,
                     [&](const QString &line) { rawLines.append(line); });
    QObject::connect(&session, &CodexAppServerSession::finished,
                     [&](int) { finished = true; });
    session.start(temp.path(), QStringList(), QStringLiteral("continue"),
                  QStringLiteral("old-thread"), QStringLiteral("gpt-plan"),
                  QStringLiteral("Plan mode"), QStringLiteral("xhigh"));
    check(pump([&] {
              return rawContainsMethod(rawLines, QStringLiteral("turn/started"));
          }),
          "resumed turn reaches active state");
    session.sendUserText(QStringLiteral("steer now"));
    check(pump([&] { return finished; }), "steered resume session completes");

    const QList<QJsonObject> sent = readMessages(logPath);
    const QJsonObject resume = firstMethod(sent, QStringLiteral("thread/resume"));
    const QJsonObject resumeParams = resume.value(QStringLiteral("params")).toObject();
    check(resumeParams.value(QStringLiteral("threadId")).toString() ==
                  QStringLiteral("old-thread") &&
              resumeParams.value(QStringLiteral("approvalPolicy")).toString() ==
                  QStringLiteral("on-request") &&
              resumeParams.value(QStringLiteral("sandbox")).toString() ==
                  QStringLiteral("read-only"),
          "Plan mode resumes with on-request/read-only policy");
    const QJsonObject startParams =
        firstMethod(sent, QStringLiteral("turn/start"))
            .value(QStringLiteral("params"))
            .toObject();
    const QJsonObject collaboration =
        startParams.value(QStringLiteral("collaborationMode")).toObject();
    check(collaboration.value(QStringLiteral("mode")).toString() ==
                  QStringLiteral("plan") &&
              collaboration.value(QStringLiteral("settings"))
                      .toObject()
                      .value(QStringLiteral("model"))
                      .toString() == QStringLiteral("gpt-plan") &&
              collaboration.value(QStringLiteral("settings"))
                      .toObject()
                      .value(QStringLiteral("reasoning_effort"))
                      .toString() == QStringLiteral("xhigh"),
          "Plan turn carries Codex collaborationMode settings");
    const QJsonObject steerParams =
        firstMethod(sent, QStringLiteral("turn/steer"))
            .value(QStringLiteral("params"))
            .toObject();
    check(steerParams.value(QStringLiteral("threadId")).toString() ==
                  QStringLiteral("thread-resumed") &&
              steerParams.value(QStringLiteral("expectedTurnId")).toString() ==
                  QStringLiteral("turn-resumed") &&
              steerParams.value(QStringLiteral("input"))
                      .toArray()
                      .first()
                      .toObject()
                      .value(QStringLiteral("text"))
                      .toString() == QStringLiteral("steer now"),
          "active user text uses turn/steer with expected turn id");
    check(session.threadId() == QStringLiteral("thread-resumed") &&
              eventCount(events, QStringLiteral("result")) == 1,
          "resumed thread id and result are retained");
}

void runModeMappingTest(const QString &executable, const QString &mode,
                        const QString &approval, const QString &sandbox)
{
    QTemporaryDir temp;
    const QString logPath = temp.filePath(QStringLiteral("mode.jsonl"));
    CodexAppServerSession session;
    session.setAppServerCommand(executable,
                                {QStringLiteral("--codex-stub"),
                                 QStringLiteral("mode"), logPath});
    bool finished = false;
    QObject::connect(&session, &CodexAppServerSession::finished,
                     [&](int) { finished = true; });
    session.start(temp.path(), QStringList(), QStringLiteral("test mode"), QString(),
                  QStringLiteral("gpt-test"), mode, QString());
    check(pump([&] { return finished; }), "mode mapping stub completes");
    const QJsonObject params =
        firstMethod(readMessages(logPath), QStringLiteral("thread/start"))
            .value(QStringLiteral("params"))
            .toObject();
    check(params.value(QStringLiteral("approvalPolicy")).toString() == approval &&
              params.value(QStringLiteral("sandbox")).toString() == sandbox,
          qPrintable(QStringLiteral("%1 maps to %2/%3")
                         .arg(mode, approval, sandbox)));
}

void runWorktreeMetadataSandboxTest(const QString &executable)
{
    QTemporaryDir temp;
    check(temp.isValid(), "worktree sandbox temp dir created");
    const QString repo = temp.filePath(QStringLiteral("repo"));
    const QString metadata = temp.filePath(QStringLiteral("metadata"));
    QDir().mkpath(repo);
    QDir().mkpath(metadata + QStringLiteral("/worktree"));
    QDir().mkpath(metadata + QStringLiteral("/common"));
    QFile dotGit(repo + QStringLiteral("/.git"));
    check(dotGit.open(QIODevice::WriteOnly | QIODevice::Text),
          "worktree git file can be created");
    dotGit.write("gitdir: ../metadata/worktree\n");
    dotGit.close();
    QFile commonDir(metadata + QStringLiteral("/worktree/commondir"));
    check(commonDir.open(QIODevice::WriteOnly | QIODevice::Text),
          "worktree common-dir file can be created");
    commonDir.write("../common\n");
    commonDir.close();

    const QString logPath = temp.filePath(QStringLiteral("worktree.jsonl"));
    CodexAppServerSession session;
    session.setAppServerCommand(executable,
                                {QStringLiteral("--codex-stub"),
                                 QStringLiteral("mode"), logPath});
    bool finished = false;
    QObject::connect(&session, &CodexAppServerSession::finished,
                     [&](int) { finished = true; });
    session.start(repo, QStringList(), QStringLiteral("commit"), QString(),
                  QStringLiteral("gpt-test"), QStringLiteral("Auto"), QString());
    check(pump([&] { return finished; }), "worktree sandbox stub completes");
    const QJsonObject policy =
        firstMethod(readMessages(logPath), QStringLiteral("turn/start"))
            .value(QStringLiteral("params"))
            .toObject()
            .value(QStringLiteral("sandboxPolicy"))
            .toObject();
    const QJsonArray roots = policy.value(QStringLiteral("writableRoots")).toArray();
    check(roots.contains(QDir(metadata + QStringLiteral("/worktree")).absolutePath()) &&
              roots.contains(QDir(metadata + QStringLiteral("/common")).absolutePath()),
          "Auto mode permits linked-worktree Git metadata writes");
}

void runInterruptedTurnTest(const QString &executable)
{
    QTemporaryDir temp;
    const QString logPath = temp.filePath(QStringLiteral("interrupted.jsonl"));
    CodexAppServerSession session;
    session.setAppServerCommand(executable,
                                {QStringLiteral("--codex-stub"),
                                 QStringLiteral("interrupted"), logPath});
    QList<QJsonObject> events;
    bool finished = false;
    QObject::connect(&session, &CodexAppServerSession::event,
                     [&](const QJsonObject &event) { events.append(event); });
    QObject::connect(&session, &CodexAppServerSession::finished,
                     [&](int) { finished = true; });
    session.start(temp.path(), QStringList(), QStringLiteral("interrupt me"));
    check(pump([&] { return finished; }), "interrupted turn stub completes");
    QJsonObject result;
    for (const QJsonObject &event : events) {
        if (event.value(QStringLiteral("type")).toString() ==
            QStringLiteral("result"))
            result = event;
    }
    check(result.value(QStringLiteral("is_error")).toBool() &&
              result.value(QStringLiteral("subtype")).toString() ==
                  QStringLiteral("interrupted"),
          "interrupted turns cannot be mistaken for successful completion");
}

void runRejectedSteerTest(const QString &executable)
{
    QTemporaryDir temp;
    const QString logPath = temp.filePath(QStringLiteral("steer-reject.jsonl"));
    CodexAppServerSession session;
    session.setAppServerCommand(executable,
                                {QStringLiteral("--codex-stub"),
                                 QStringLiteral("steer-reject"), logPath});
    QStringList rawLines;
    QList<QJsonObject> events;
    bool finished = false;
    QObject::connect(&session, &CodexAppServerSession::rawLine,
                     [&](const QString &line) { rawLines.append(line); });
    QObject::connect(&session, &CodexAppServerSession::finished,
                     [&](int) { finished = true; });
    QObject::connect(&session, &CodexAppServerSession::event,
                     [&](const QJsonObject &event) { events.append(event); });
    session.start(temp.path(), QStringList(), QStringLiteral("initial"),
                  QStringLiteral("old-thread"));
    check(pump([&] {
              return rawContainsMethod(rawLines, QStringLiteral("turn/started"));
          }),
          "rejected-steer turn reaches active state");
    session.sendUserText(QStringLiteral("do not lose me"));
    check(pump([&] { return finished; }),
          "rejected steer is replayed and completes");
    const QList<QJsonObject> sent = readMessages(logPath);
    QList<QJsonObject> turnStarts;
    int steerRequests = 0;
    for (const QJsonObject &message : sent) {
        if (message.value(QStringLiteral("method")).toString() ==
            QStringLiteral("turn/start"))
            turnStarts.append(message);
        if (message.value(QStringLiteral("method")).toString() ==
            QStringLiteral("turn/steer"))
            ++steerRequests;
    }
    check(steerRequests == 1 && turnStarts.size() == 2 &&
              turnStarts.last()
                      .value(QStringLiteral("params"))
                      .toObject()
                      .value(QStringLiteral("input"))
                      .toArray()
                      .first()
                      .toObject()
                      .value(QStringLiteral("text"))
                      .toString() == QStringLiteral("do not lose me"),
          "rejected steer waits for completion, then replays as a fresh turn");
    check(eventCount(events, QStringLiteral("result")) == 1,
          "steer fallback emits only the replayed turn's terminal result");
}

void runResumeFailureTest(const QString &executable)
{
    QTemporaryDir temp;
    const QString logPath = temp.filePath(QStringLiteral("resume-error.jsonl"));
    CodexAppServerSession session;
    session.setAppServerCommand(executable,
                                {QStringLiteral("--codex-stub"),
                                 QStringLiteral("resume-error"), logPath});
    QList<QJsonObject> events;
    bool finished = false;
    QObject::connect(&session, &CodexAppServerSession::event,
                     [&](const QJsonObject &event) { events.append(event); });
    QObject::connect(&session, &CodexAppServerSession::finished,
                     [&](int) { finished = true; });
    session.start(temp.path(), QStringList(), QStringLiteral("resume"),
                  QStringLiteral("missing-thread"));
    check(pump([&] { return finished; }), "failed resume terminates transport");
    QJsonObject terminalResult;
    for (const QJsonObject &event : events) {
        if (event.value(QStringLiteral("type")).toString() ==
                QStringLiteral("result") &&
            event.value(QStringLiteral("is_error")).toBool())
            terminalResult = event;
    }
    check(terminalResult.value(QStringLiteral("thread_id")).toString() ==
                  QStringLiteral("missing-thread") &&
              terminalResult.value(QStringLiteral("result"))
                  .toString()
                  .contains(QStringLiteral("thread missing")),
          "failed resume emits a terminal error before finished");
}

void runInterruptTest(const QString &executable)
{
    QTemporaryDir temp;
    const QString logPath = temp.filePath(QStringLiteral("interrupt.jsonl"));
    CodexAppServerSession session;
    session.setAppServerCommand(executable,
                                {QStringLiteral("--codex-stub"),
                                 QStringLiteral("interrupt"), logPath});
    QStringList rawLines;
    bool finished = false;
    QObject::connect(&session, &CodexAppServerSession::rawLine,
                     [&](const QString &line) { rawLines.append(line); });
    QObject::connect(&session, &CodexAppServerSession::finished,
                     [&](int) { finished = true; });
    session.start(temp.path(), QStringList(), QStringLiteral("long turn"));
    check(pump([&] {
              return rawContainsMethod(rawLines, QStringLiteral("turn/started"));
          }),
          "interrupt test reaches an active turn");
    session.interrupt();
    check(pump([&] { return finished; }), "interrupt request completes the stub");
    const QJsonObject params =
        firstMethod(readMessages(logPath), QStringLiteral("turn/interrupt"))
            .value(QStringLiteral("params"))
            .toObject();
    check(params.value(QStringLiteral("threadId")).toString() ==
                  QStringLiteral("thread-fresh") &&
              params.value(QStringLiteral("turnId")).toString() ==
                  QStringLiteral("turn-fresh"),
          "interrupt sends the exact active thread and turn ids");
}

void runTurnOptionsTest(const QString &executable)
{
    QTemporaryDir temp;
    const QString logPath = temp.filePath(QStringLiteral("options.jsonl"));
    CodexAppServerSession session;
    session.setAppServerCommand(executable,
                                {QStringLiteral("--codex-stub"),
                                 QStringLiteral("options"), logPath});
    QList<QJsonObject> events;
    bool finished = false;
    QObject::connect(&session, &CodexAppServerSession::event,
                     [&](const QJsonObject &event) { events.append(event); });
    QObject::connect(&session, &CodexAppServerSession::finished,
                     [&](int) { finished = true; });
    session.start(temp.path(), QStringList(), QStringLiteral("first"), QString(),
                  QStringLiteral("gpt-old"), QStringLiteral("Auto mode"),
                  QStringLiteral("low"));
    check(pump([&] {
              return eventCount(events, QStringLiteral("result")) == 1 &&
                     session.running();
          }),
          "turn-options stub completes its initial turn");
    session.setTurnOptions(QStringLiteral("gpt-new"), QStringLiteral("Plan mode"),
                           QStringLiteral("xhigh"));
    session.sendUserText(QStringLiteral("second"));
    check(pump([&] {
              return eventCount(events, QStringLiteral("result")) == 2 &&
                     session.running();
          }),
          "updated Plan turn options complete");
    session.setTurnOptions(QStringLiteral("gpt-final"),
                           QStringLiteral("Edit automatically"),
                           QStringLiteral("medium"));
    session.sendUserText(QStringLiteral("third"));
    check(pump([&] { return finished; }), "leaving Plan mode completes");

    QList<QJsonObject> starts;
    for (const QJsonObject &message : readMessages(logPath)) {
        if (message.value(QStringLiteral("method")).toString() ==
            QStringLiteral("turn/start"))
            starts.append(message);
    }
    const QJsonObject params = starts.size() > 1
                                   ? starts.at(1)
                                         .value(QStringLiteral("params"))
                                         .toObject()
                                   : QJsonObject();
    const QJsonObject collaboration =
        params.value(QStringLiteral("collaborationMode")).toObject();
    check(starts.size() == 3 &&
              params.value(QStringLiteral("model")).toString() ==
                  QStringLiteral("gpt-new") &&
              params.value(QStringLiteral("effort")).toString() ==
                  QStringLiteral("xhigh") &&
              collaboration.value(QStringLiteral("mode")).toString() ==
                  QStringLiteral("plan") &&
              collaboration.value(QStringLiteral("settings"))
                      .toObject()
                      .value(QStringLiteral("model"))
                      .toString() == QStringLiteral("gpt-new") &&
              params.value(QStringLiteral("approvalPolicy")).toString() ==
                  QStringLiteral("on-request") &&
              params.value(QStringLiteral("sandboxPolicy"))
                      .toObject()
                      .value(QStringLiteral("type"))
                      .toString() == QStringLiteral("readOnly"),
          "setTurnOptions applies model, effort, and plan collaboration next turn");
    const QJsonObject finalParams =
        starts.size() > 2
            ? starts.at(2).value(QStringLiteral("params")).toObject()
            : QJsonObject();
    check(finalParams.value(QStringLiteral("model")).toString() ==
                  QStringLiteral("gpt-final") &&
              finalParams.value(QStringLiteral("effort")).toString() ==
                  QStringLiteral("medium") &&
              finalParams.value(QStringLiteral("collaborationMode")).isNull() &&
              finalParams.value(QStringLiteral("sandboxPolicy"))
                      .toObject()
                      .value(QStringLiteral("type"))
                      .toString() == QStringLiteral("workspaceWrite"),
          "leaving Plan clears collaboration mode and restores workspace sandbox");
}

void runFatalErrorTest(const QString &executable)
{
    QTemporaryDir temp;
    const QString logPath = temp.filePath(QStringLiteral("fatal-error.jsonl"));
    CodexAppServerSession session;
    session.setAppServerCommand(executable,
                                {QStringLiteral("--codex-stub"),
                                 QStringLiteral("fatal-error"), logPath});
    QList<QJsonObject> events;
    bool finished = false;
    QObject::connect(&session, &CodexAppServerSession::event,
                     [&](const QJsonObject &event) { events.append(event); });
    QObject::connect(&session, &CodexAppServerSession::finished,
                     [&](int) { finished = true; });
    session.start(temp.path(), QStringList(), QStringLiteral("fail"));
    check(pump([&] { return finished; }), "fatal error stub completes");
    QJsonObject result;
    for (const QJsonObject &event : events) {
        if (event.value(QStringLiteral("type")).toString() ==
            QStringLiteral("result"))
            result = event;
    }
    check(eventCount(events, QStringLiteral("result")) == 1 &&
              result.value(QStringLiteral("is_error")).toBool() &&
              result.value(QStringLiteral("result"))
                  .toString()
                  .contains(QStringLiteral("fatal turn")),
          "non-retrying error emits one terminal result");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() > 1 && arguments.at(1) == QStringLiteral("--codex-stub"))
        return runStub(arguments);

    const QString executable = QCoreApplication::applicationFilePath();
    runFreshProtocolTest(executable);
    runResumeAndSteerTest(executable);
    runModeMappingTest(executable, QStringLiteral("Ask before edits"),
                       QStringLiteral("untrusted"),
                       QStringLiteral("workspace-write"));
    runModeMappingTest(executable, QStringLiteral("Edit automatically"),
                       QStringLiteral("on-request"),
                       QStringLiteral("workspace-write"));
    runWorktreeMetadataSandboxTest(executable);
    runInterruptedTurnTest(executable);
    runRejectedSteerTest(executable);
    runResumeFailureTest(executable);
    runInterruptTest(executable);
    runTurnOptionsTest(executable);
    runFatalErrorTest(executable);

    if (failures == 0)
        qInfo("All Codex app-server protocol tests passed.");
    else
        qCritical("%d Codex app-server protocol test(s) failed.", failures);
    return failures == 0 ? 0 : 1;
}
