// MainWindowSourceControl: MainWindow feature methods, split out of MainWindow.cpp.
// Source Control panel: working-tree changes, staging, and commits.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"

#include <algorithm>

using namespace forkmesh::ui;

// ---- Source Control panel (working-tree changes) ---------------------------

// Human-readable name for a `git status --porcelain` status letter.
static QString scmStatusTip(QChar status)
{
    switch (status.toLatin1()) {
    case 'M': return QStringLiteral("Modified");
    case 'A': return QStringLiteral("Added");
    case 'D': return QStringLiteral("Deleted");
    case 'R': return QStringLiteral("Renamed");
    case 'C': return QStringLiteral("Copied");
    case 'U': return QStringLiteral("Unmerged (conflict)");
    case 'T': return QStringLiteral("Type changed");
    case '?': return QStringLiteral("Untracked");
    default: return QStringLiteral("Changed");
    }
}

QWidget *MainWindow::buildSourceControlPanel()
{
    auto *panel = new QWidget;
    auto *root = new QVBoxLayout(panel);
    root->setContentsMargins(16, 10, 16, 6);
    root->setSpacing(6);

    // Single-line compose strip on top of the changes: the message field, inline
    // AI generation (no popup), a live character count, and the stage/commit
    // controls — all on one row.
    m_scmMessage = new QLineEdit;
    m_scmMessage->setObjectName("messageInput");
    m_scmMessage->setClearButtonEnabled(true);
    m_scmMessage->setPlaceholderText("Message (Ctrl+Enter to commit)");
    auto *commitShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_scmMessage);
    commitShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(commitShortcut, &QShortcut::activated, this, &MainWindow::scmCommit);

    m_scmGenerateButton = new QPushButton(QString::fromUtf8("\xE2\x9C\xA8 Generate"));
    m_scmGenerateButton->setCursor(Qt::PointingHandCursor);
    m_scmGenerateButton->setToolTip(
        "Draft the message from the changes using Claude or OpenAI");
    // A filled accent so the AI action clearly stands out from the ghost buttons.
    m_scmGenerateButton->setStyleSheet(
        "QPushButton{background:#8957e5;color:#ffffff;border:1px solid #8957e5;"
        "border-radius:6px;padding:4px 12px;font-weight:600;}"
        "QPushButton:hover{background:#9a6ff0;border-color:#9a6ff0;}"
        "QPushButton:disabled{background:#6e6a86;border-color:#6e6a86;"
        "color:#d9d9e3;}");
    connect(m_scmGenerateButton, &QPushButton::clicked, this,
            &MainWindow::generateScmMessage);

    m_scmGenModel = new QComboBox;
    for (int i = 0; i < kScmAiModelCount; ++i)
        m_scmGenModel->addItem(QString::fromLatin1(kScmAiModels[i].label), i);
    // On-device option: no model, no API key, no cost — drafts the message
    // locally from the diff's structure. The sentinel data -1 routes
    // generateScmMessage() to the heuristic path. It's the default: free, instant
    // and private, and it auto-fills as the changes change (see autoFillScmMessage).
    m_scmGenModel->addItem(QStringLiteral("On-device (no AI)"), -1);
    m_scmGenModel->setCurrentIndex(m_scmGenModel->findData(-1));
    m_scmGenModel->setToolTip(
        "How to draft the message: on-device (no AI), or a Claude/OpenAI model");
    // Switching engine: auto-fill immediately when on-device is chosen (no-op for
    // the paid models, which only run on an explicit Generate click).
    connect(m_scmGenModel, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { autoFillScmMessage(); });

    m_scmGenKind = new QComboBox;
    m_scmGenKind->addItem("Commit message");
    m_scmGenKind->addItem("X post");
    m_scmGenKind->setToolTip("What to generate from the changes");

    // How far back to look for the changes being described: just the current
    // uncommitted edits, everything since an hour ago, or everything since
    // midnight (each window also includes the current uncommitted edits).
    m_scmGenDuration = new QComboBox;
    m_scmGenDuration->addItem("Current changes");
    m_scmGenDuration->addItem("Past hour");
    m_scmGenDuration->addItem("All day");
    m_scmGenDuration->setToolTip("Which changes to summarise");
    // When the user switches to "Past hour" or "All day", enable the Generate
    // button immediately — commits exist in those windows even with a clean tree.
    connect(m_scmGenDuration, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                // Re-evaluate the Generate button enable state now that the
                // duration scope has changed.
                refreshSourceControl();
            });

    m_scmCopyButton = new QPushButton("Copy");
    m_scmCopyButton->setToolTip("Copy the message to the clipboard");
    setOcticon(m_scmCopyButton, "copy", 14);
    connect(m_scmCopyButton, &QPushButton::clicked, this, [this] {
        const QString text =
            m_scmMessage ? m_scmMessage->text().trimmed() : QString();
        if (text.isEmpty()) {
            if (m_scmGenStatus)
                m_scmGenStatus->setText("Nothing to copy.");
            return;
        }
        QApplication::clipboard()->setText(text);
        if (m_scmGenStatus)
            m_scmGenStatus->setText("Copied.");
    });

    // Live character count (and last generation cost). For an X post it shows the
    // 280-char budget; otherwise a plain count.
    m_scmGenStatus = new QLabel;
    m_scmGenStatus->setObjectName("statusLine");
    m_scmGenStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto updateCharCount = [this] {
        if (!m_scmGenStatus || !m_scmMessage)
            return;
        const int n = m_scmMessage->text().size();
        const bool tweet = m_scmGenKind && m_scmGenKind->currentIndex() == 1;
        m_scmGenStatus->setText(tweet ? QStringLiteral("%1/280").arg(n)
                                      : QStringLiteral("%1 chars").arg(n));
    };
    connect(m_scmMessage, &QLineEdit::textChanged, this, updateCharCount);
    connect(m_scmGenKind, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [updateCharCount](int) { updateCharCount(); });
    updateCharCount();

    m_scmStageAllButton = new QPushButton("Stage all");
    connect(m_scmStageAllButton, &QPushButton::clicked, this, &MainWindow::scmStageAll);
    m_scmUnstageAllButton = new QPushButton("Unstage all");
    connect(m_scmUnstageAllButton, &QPushButton::clicked, this, &MainWindow::scmUnstageAll);
    m_scmDiscardAllButton = new QPushButton("Discard all");
    connect(m_scmDiscardAllButton, &QPushButton::clicked, this, &MainWindow::scmDiscardAll);
    m_scmCommitButton = new QPushButton("Commit");
    m_scmCommitButton->setObjectName("primaryButton");
    connect(m_scmCommitButton, &QPushButton::clicked, this, &MainWindow::scmCommit);
    m_scmCommitPushButton = new QPushButton("Commit & push");
    m_scmCommitPushButton->setObjectName("primaryButton");
    m_scmCommitPushButton->setToolTip(
        "Commit the staged changes, then publish them to the network mirror "
        "(or push to the upstream branch).");
    connect(m_scmCommitPushButton, &QPushButton::clicked, this,
            &MainWindow::scmCommitAndPush);
    m_scmStageCommitPushButton = new QPushButton("Stage all, commit & push");
    m_scmStageCommitPushButton->setObjectName("primaryButton");
    m_scmStageCommitPushButton->setToolTip(
        "Stage every change, commit them, then publish to the network mirror "
        "(or push to the upstream branch) — in one click.");
    connect(m_scmStageCommitPushButton, &QPushButton::clicked, this,
            &MainWindow::scmStageAllCommitAndPush);
    for (QPushButton *b : {m_scmCopyButton, m_scmStageAllButton,
                           m_scmUnstageAllButton, m_scmDiscardAllButton,
                           m_scmCommitButton, m_scmCommitPushButton,
                           m_scmStageCommitPushButton}) {
        if (b != m_scmCommitButton && b != m_scmCommitPushButton &&
            b != m_scmStageCommitPushButton)
            b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }

    auto *composeRow = new QHBoxLayout;
    composeRow->setContentsMargins(0, 0, 0, 0);
    composeRow->setSpacing(6);
    composeRow->addWidget(m_scmMessage, 1);
    composeRow->addWidget(m_scmGenerateButton);
    composeRow->addWidget(m_scmGenModel);
    composeRow->addWidget(m_scmGenKind);
    composeRow->addWidget(m_scmGenDuration);
    composeRow->addWidget(m_scmCopyButton);
    composeRow->addWidget(m_scmGenStatus);
    composeRow->addWidget(m_scmStageAllButton);
    composeRow->addWidget(m_scmUnstageAllButton);
    composeRow->addWidget(m_scmDiscardAllButton);
    composeRow->addWidget(m_scmCommitButton);
    composeRow->addWidget(m_scmCommitPushButton);
    composeRow->addWidget(m_scmStageCommitPushButton);
    root->addLayout(composeRow);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel("CHANGES");
    title->setObjectName("sectionLabel");
    m_scmCountLabel = new QLabel;
    m_scmCountLabel->setObjectName("statusLine");

    // Up/down step through the changed files without touching the tree directly,
    // so you can review each diff in turn from the keyboard or mouse.
    m_scmPrevButton = new QPushButton;
    m_scmPrevButton->setToolTip("Previous change");
    setOcticon(m_scmPrevButton, "chevron-up", 14);
    connect(m_scmPrevButton, &QPushButton::clicked, this,
            [this] { scmSelectAdjacentChange(-1); });
    m_scmNextButton = new QPushButton;
    m_scmNextButton->setToolTip("Next change");
    setOcticon(m_scmNextButton, "chevron-down", 14);
    connect(m_scmNextButton, &QPushButton::clicked, this,
            [this] { scmSelectAdjacentChange(1); });

    m_scmRefreshButton = new QPushButton;
    setOcticon(m_scmRefreshButton, "sync", 14);
    m_scmRefreshButton->setToolTip("Rescan the working tree for changes");
    connect(m_scmRefreshButton, &QPushButton::clicked, this,
            &MainWindow::refreshSourceControl);
    addRefreshSpin(m_scmRefreshButton);

    for (QPushButton *b : {m_scmPrevButton, m_scmNextButton, m_scmRefreshButton}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }

    header->addWidget(title);
    header->addWidget(m_scmCountLabel);
    header->addStretch();
    header->addWidget(m_scmPrevButton);
    header->addWidget(m_scmNextButton);
    header->addWidget(m_scmRefreshButton);
    root->addLayout(header);

    m_scmTree = new QTreeWidget;
    m_scmTree->setObjectName("fileTree");
    enableHoverRowHighlight(m_scmTree); // green outline selection (issue #252)
    m_scmTree->setColumnCount(2);
    m_scmTree->setHeaderHidden(true);
    m_scmTree->setMinimumWidth(240);
    m_scmTree->setRootIsDecorated(true);
    m_scmTree->header()->setStretchLastSection(false);
    m_scmTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_scmTree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_scmTree->setColumnWidth(1, 84);
    connect(m_scmTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                if (!item)
                    return;
                const QString path = item->data(0, Qt::UserRole).toString();
                if (path.isEmpty())
                    return; // group header
                showScmDiff(path, item->data(0, Qt::UserRole + 1).toBool(),
                            item->data(0, Qt::UserRole + 2).toBool());
            });

    m_scmDiff = new QTextBrowser;
    m_scmDiff->setObjectName("diffView");
    m_scmDiff->setLineWrapMode(QTextEdit::NoWrap);
    registerDiffView(m_scmDiff);

    auto *bodySplit = new QSplitter(Qt::Horizontal);
    bodySplit->setChildrenCollapsible(false);
    bodySplit->addWidget(m_scmTree);
    bodySplit->addWidget(m_scmDiff);
    bodySplit->setStretchFactor(0, 0);
    bodySplit->setStretchFactor(1, 1);
    bodySplit->setSizes({360, 520});
    root->addWidget(bodySplit, 1);

    m_scmEmptyNote =
        new QLabel("No working tree on this node \xE2\x80\x94 changes are read-only here.");
    m_scmEmptyNote->setObjectName("statusLine");
    m_scmEmptyNote->setAlignment(Qt::AlignCenter);
    m_scmEmptyNote->hide();
    root->addWidget(m_scmEmptyNote);

    return panel;
}

void MainWindow::refreshSourceControl()
{
    if (!m_scmTree)
        return;
    const QString dir = repoGitDir();
    const bool canWrite = !dir.isEmpty() && repoHasWorkingTree();
    if (m_scmEmptyNote)
        m_scmEmptyNote->setVisible(!canWrite);
    for (QWidget *w : {static_cast<QWidget *>(m_scmMessage),
                       static_cast<QWidget *>(m_scmTree),
                       static_cast<QWidget *>(m_scmStageAllButton),
                       static_cast<QWidget *>(m_scmUnstageAllButton),
                       static_cast<QWidget *>(m_scmDiscardAllButton)})
        if (w)
            w->setEnabled(canWrite);

    if (!canWrite) {
        m_scmStatusCache.clear();
        m_scmDiffCache.clear();
        m_scmTree->clear();
        if (m_scmDiff)
            m_scmDiff->clear();
        if (m_scmCountLabel)
            m_scmCountLabel->clear();
        if (m_scmCommitButton)
            m_scmCommitButton->setEnabled(false);
        if (m_scmCommitPushButton)
            m_scmCommitPushButton->setEnabled(false);
        if (m_scmStageCommitPushButton)
            m_scmStageCommitPushButton->setEnabled(false);
        if (m_scmGenerateButton)
            m_scmGenerateButton->setEnabled(false);
        return;
    }

    QByteArray out;
    runGitCapture(dir, {"status", "--porcelain=v1", "-z"}, &out, nullptr);

    // Skip the full rebuild when the working tree is unchanged since the last
    // scan. This matters now that we rescan on tab focus / window activation:
    // without it, every rescan would clear the tree (losing the open diff and the
    // selection) and flicker even when nothing moved.
    if (out == m_scmStatusCache && m_scmTree->topLevelItemCount() > 0)
        return;
    m_scmStatusCache = out;
    m_scmDiffCache.clear(); // the tree changed, so any cached diffs are stale

    // Remember which file's diff is showing so the rebuild can restore it instead
    // of dropping the user back to a blank diff view.
    QString prevPath;
    bool prevStaged = false;
    if (QTreeWidgetItem *cur = m_scmTree->currentItem()) {
        prevPath = cur->data(0, Qt::UserRole).toString();
        prevStaged = cur->data(0, Qt::UserRole + 1).toBool();
    }

    m_scmTree->clear();
    if (m_scmDiff)
        m_scmDiff->clear();

    struct Row {
        QString path;
        QChar status;
        bool staged;
        bool untracked;
    };
    QList<Row> staged, changes;
    const QList<QByteArray> fields = out.split('\0');
    for (int i = 0; i < fields.size(); ++i) {
        const QByteArray f = fields.at(i);
        if (f.size() < 3)
            continue; // "XY <path>"
        const char x = f.at(0), y = f.at(1);
        const QString path = QString::fromUtf8(f.mid(3));
        // Renames/copies carry the original path in the following NUL field.
        if (x == 'R' || x == 'C')
            ++i;
        if (x == '?' && y == '?') {
            changes.append({path, QChar('?'), false, true});
            continue;
        }
        if (x != ' ')
            staged.append({path, QChar(x), true, false});
        if (y != ' ')
            changes.append({path, QChar(y), false, false});
    }

    auto addGroup = [this](const QString &name, const QList<Row> &rows,
                           bool stagedGroup) {
        if (rows.isEmpty())
            return;
        auto *group = new QTreeWidgetItem(m_scmTree);
        group->setFirstColumnSpanned(true);

        // VS Code-style group header: a bold "Name (N)" label with hover actions
        // floated to the right — Open Changes (a combined diff of the whole group)
        // plus Stage/Unstage all, and Discard all for unstaged changes. The label
        // lives in the row widget (not setText) so the buttons can sit at the far
        // right of the full-width header row, the way the per-file actions do.
        auto *gw = new QWidget;
        auto *gh = new QHBoxLayout(gw);
        gh->setContentsMargins(0, 0, 6, 0);
        gh->setSpacing(0);
        auto *gl =
            new QLabel(QStringLiteral("%1 (%2)").arg(name).arg(rows.size()), gw);
        QFont gf = gl->font();
        gf.setBold(true);
        gl->setFont(gf);
        gh->addWidget(gl);
        gh->addStretch();
        auto groupBtn = [&](const QString &glyph, const QString &tip) {
            auto *b = new QToolButton(gw);
            b->setText(glyph);
            b->setToolTip(tip);
            b->setAutoRaise(true);
            b->setCursor(Qt::PointingHandCursor);
            return b;
        };
        auto *openAll = new QToolButton(gw);
        openAll->setIcon(themedOcticon("diff", QColor("#8b949e"), 14));
        openAll->setToolTip("Open all changes");
        openAll->setAutoRaise(true);
        openAll->setCursor(Qt::PointingHandCursor);
        connect(openAll, &QToolButton::clicked, this,
                [this, stagedGroup] { showScmDiffAll(stagedGroup); });
        gh->addWidget(openAll);
        if (stagedGroup) {
            auto *u =
                groupBtn(QString::fromUtf8("\xE2\x88\x92"), "Unstage all changes");
            connect(u, &QToolButton::clicked, this, &MainWindow::scmUnstageAll);
            gh->addWidget(u);
        } else {
            auto *d =
                groupBtn(QString::fromUtf8("\xE2\x86\xBA"), "Discard all changes");
            connect(d, &QToolButton::clicked, this, &MainWindow::scmDiscardAll);
            auto *s = groupBtn(QStringLiteral("+"), "Stage all changes");
            connect(s, &QToolButton::clicked, this, &MainWindow::scmStageAll);
            gh->addWidget(d);
            gh->addWidget(s);
        }
        m_scmTree->setItemWidget(group, 0, gw);

        for (const Row &r : rows) {
            auto *item = new QTreeWidgetItem(group);
            item->setIcon(0, iconForFile(r.path.section('/', -1)));
            item->setText(0, r.path);
            item->setToolTip(0, r.path);
            item->setData(0, Qt::UserRole, r.path);
            item->setData(0, Qt::UserRole + 1, r.staged);
            item->setData(0, Qt::UserRole + 2, r.untracked);

            auto *w = new QWidget;
            auto *h = new QHBoxLayout(w);
            h->setContentsMargins(0, 0, 6, 0);
            h->setSpacing(0);
            auto *statusLabel = new QLabel(QString(r.status), w);
            statusLabel->setToolTip(scmStatusTip(r.status));
            QFont sf = statusLabel->font();
            sf.setBold(true);
            statusLabel->setFont(sf);
            h->addWidget(statusLabel);
            h->addStretch();
            auto makeBtn = [&](const QString &glyph, const QString &tip) {
                auto *b = new QToolButton(w);
                b->setText(glyph);
                b->setToolTip(tip);
                b->setAutoRaise(true);
                b->setCursor(Qt::PointingHandCursor);
                return b;
            };
            const QString path = r.path;
            const bool untracked = r.untracked;
            if (r.staged) {
                auto *u = makeBtn(QString::fromUtf8("\xE2\x88\x92"), "Unstage");
                connect(u, &QToolButton::clicked, this,
                        [this, path] { scmUnstagePath(path); });
                h->addWidget(u);
            } else {
                auto *d = makeBtn(QString::fromUtf8("\xE2\x86\xBA"), "Discard changes");
                connect(d, &QToolButton::clicked, this,
                        [this, path, untracked] { scmDiscardPath(path, untracked); });
                auto *s = makeBtn(QStringLiteral("+"), "Stage");
                connect(s, &QToolButton::clicked, this,
                        [this, path] { scmStagePath(path); });
                h->addWidget(d);
                h->addWidget(s);
            }
            // The per-row buttons sit in column 1 (the file label + status stay in
            // column 0), so both the name and the actions are always visible.
            m_scmTree->setItemWidget(item, 1, w);
        }
        group->setExpanded(true);
    };
    addGroup("Staged Changes", staged, /*stagedGroup=*/true);
    addGroup("Changes", changes, /*stagedGroup=*/false);
    // Widen the tree so the full relative paths are always visible (capped so the
    // diff still has room); the user can drag the splitter wider from there.
    fitTreeToWidestEntry(m_scmTree);

    const int total = staged.size() + changes.size();
    if (m_scmCountLabel)
        m_scmCountLabel->setText(total ? QString::number(total) : QString());
    const bool anything = total > 0;
    if (m_scmCommitButton)
        m_scmCommitButton->setEnabled(anything);
    if (m_scmCommitPushButton)
        m_scmCommitPushButton->setEnabled(anything);
    if (m_scmStageCommitPushButton)
        m_scmStageCommitPushButton->setEnabled(anything);
    if (m_scmGenerateButton) {
        // Enable Generate when there are current changes, OR when the
        // duration dropdown covers committed history (past hour / all day)
        // — in those modes, commits exist even with a clean working tree.
        const bool durCoversHistory =
            m_scmGenDuration && m_scmGenDuration->currentIndex() > 0;
        m_scmGenerateButton->setEnabled(anything || durCoversHistory);
    }
    if (m_scmUnstageAllButton)
        m_scmUnstageAllButton->setEnabled(!staged.isEmpty());
    if (m_scmDiscardAllButton)
        m_scmDiscardAllButton->setEnabled(!changes.isEmpty());

    // On-device drafting is free, local and instant, so when it's the selected
    // engine auto-fill the message from the changes (never the paid AI models).
    autoFillScmMessage();

    // Re-select the file that was open before the rebuild (re-showing its diff) if
    // it still has changes; otherwise leave the diff cleared.
    if (!prevPath.isEmpty()) {
        for (int g = 0; g < m_scmTree->topLevelItemCount(); ++g) {
            QTreeWidgetItem *grp = m_scmTree->topLevelItem(g);
            for (int c = 0; c < grp->childCount(); ++c) {
                QTreeWidgetItem *item = grp->child(c);
                if (item->data(0, Qt::UserRole).toString() == prevPath &&
                    item->data(0, Qt::UserRole + 1).toBool() == prevStaged) {
                    m_scmTree->setCurrentItem(item);
                    return;
                }
            }
        }
    }
}

void MainWindow::showScmDiff(const QString &path, bool staged, bool untracked)
{
    if (!m_scmDiff)
        return;
    // Cache the rendered HTML per file so re-clicking a file (or walking the list
    // with the up/down buttons) is instant — the lag is the synchronous `git
    // diff` + render, which we only want to pay once per file per rescan. The
    // stylesheet carries the theme colors and is re-applied on every show, so a
    // cached body still tracks the current theme. refreshSourceControl() clears
    // this cache whenever the working tree changes.
    const QString key =
        QStringLiteral("%1|%2|%3").arg(int(staged)).arg(int(untracked)).arg(path);
    auto cached = m_scmDiffCache.constFind(key);
    if (cached != m_scmDiffCache.constEnd()) {
        setDiffHtml(m_scmDiff, *cached);
        return;
    }
    const QString dir = repoGitDir();
    QByteArray out;
    if (untracked)
        out = gitCaptureStdout(dir, {"diff", "--no-index", "--", "/dev/null", path});
    else if (staged)
        out = gitCaptureStdout(dir, {"diff", "--cached", "--", path});
    else
        out = gitCaptureStdout(dir, {"diff", "--", path});
    QList<DiffFileEntry> files;
    QString html = renderDiffHtml(QString::fromUtf8(out), files, dir,
                                  QString(), QString(), path,
                                  QHash<QString, QString>());
    if (html.isEmpty())
        html = QStringLiteral("<p style='color:#8b949e'>(no diff)</p>");
    m_scmDiffCache.insert(key, html);
    setDiffHtml(m_scmDiff, html);
}

void MainWindow::showScmDiffAll(bool staged)
{
    if (!m_scmDiff)
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;

    QByteArray out;
    if (staged) {
        out = gitCaptureStdout(dir, {"diff", "--cached"});
    } else {
        out = gitCaptureStdout(dir, {"diff"});
        // `git diff` omits untracked files; append each as a /dev/null diff so
        // "Open Changes" shows new files too, matching the per-file view.
        QByteArray others;
        runGitCapture(dir, {"ls-files", "--others", "--exclude-standard", "-z"},
                      &others, nullptr);
        for (const QByteArray &p : others.split('\0')) {
            if (p.isEmpty())
                continue;
            out += gitCaptureStdout(
                dir, {"diff", "--no-index", "--", "/dev/null", QString::fromUtf8(p)});
        }
    }

    QList<DiffFileEntry> files;
    QString html = renderDiffHtml(QString::fromUtf8(out), files, dir, QString(),
                                  QString(), QString(), QHash<QString, QString>());
    if (html.isEmpty())
        html = QStringLiteral("<p style='color:#8b949e'>(no changes)</p>");
    setDiffHtml(m_scmDiff, html);
}

void MainWindow::scmSelectAdjacentChange(int delta)
{
    if (!m_scmTree)
        return;

    // Step through the open file's hunks first; only move to the next/previous
    // file once we're already past its last/first hunk.
    QTreeWidgetItem *current = m_scmTree->currentItem();
    const bool fileOpen =
        m_scmDiff && current &&
        !current->data(0, Qt::UserRole).toString().isEmpty();
    if (fileOpen && scmScrollToAdjacentHunk(delta))
        return;

    // Flatten the changed files (skipping the group headers) into visual order so
    // the up/down buttons can step through every change regardless of grouping.
    QList<QTreeWidgetItem *> files;
    for (int g = 0; g < m_scmTree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *grp = m_scmTree->topLevelItem(g);
        for (int c = 0; c < grp->childCount(); ++c)
            files.append(grp->child(c));
    }
    if (files.isEmpty())
        return;
    const int cur = files.indexOf(current);
    int next = cur < 0 ? (delta > 0 ? 0 : files.size() - 1) : cur + delta;
    if (next < 0 || next >= files.size())
        return; // clamp at the ends rather than wrapping
    QTreeWidgetItem *target = files.at(next);
    m_scmTree->setCurrentItem(target); // fires currentItemChanged -> showScmDiff
    m_scmTree->scrollToItem(target);
    // Entering the previous file from below: land on its last hunk so prev keeps
    // walking changes upward. The next file opens scrolled to the top already, so
    // its first hunk is in view.
    if (delta < 0)
        scmScrollToAdjacentHunk(-1, /*fromEnd=*/true);
}

bool MainWindow::scmScrollToAdjacentHunk(int delta, bool fromEnd)
{
    if (!m_scmDiff)
        return false;
    if (fromEnd)
        m_scmDiff->moveCursor(QTextCursor::End);
    // Each hunk header renders as "@@ -old +new @@ ..."; the "@@ -" prefix occurs
    // exactly once per hunk, so searching for it walks the diff hunk-by-hunk.
    const QTextDocument::FindFlags flags =
        delta < 0 ? QTextDocument::FindBackward : QTextDocument::FindFlags();
    if (!m_scmDiff->find(QStringLiteral("@@ -"), flags))
        return false;
    // Keep the find's selection as the cursor (so a further step advances past
    // it), but scroll the matched hunk header up near the top of the view.
    const QTextCursor found = m_scmDiff->textCursor();
    QTextCursor lineCur(found);
    lineCur.setPosition(found.selectionStart());
    lineCur.movePosition(QTextCursor::StartOfLine);
    const QRect r = m_scmDiff->cursorRect(lineCur);
    if (QScrollBar *vbar = m_scmDiff->verticalScrollBar())
        vbar->setValue(vbar->value() + r.top() - 4);
    return true;
}

void MainWindow::scmStagePath(const QString &path)
{
    const QString dir = repoGitDir();
    QString err;
    if (!runGitCapture(dir, {"add", "-A", "--", path}, nullptr, &err))
        QMessageBox::warning(this, "Stage", err.isEmpty() ? "git add failed." : err);
    refreshSourceControl();
}

void MainWindow::scmUnstagePath(const QString &path)
{
    const QString dir = repoGitDir();
    QString err;
    if (!runGitCapture(dir, {"restore", "--staged", "--", path}, nullptr, &err) &&
        !runGitCapture(dir, {"reset", "-q", "--", path}, nullptr, nullptr))
        QMessageBox::warning(this, "Unstage", err.isEmpty() ? "git restore failed." : err);
    refreshSourceControl();
}

void MainWindow::scmDiscardPath(const QString &path, bool untracked)
{
    if (QMessageBox::warning(
            this, "Discard changes",
            QStringLiteral("Discard changes to \"%1\"? This cannot be undone.").arg(path),
            QMessageBox::Discard | QMessageBox::Cancel) != QMessageBox::Discard)
        return;
    const QString dir = repoGitDir();
    if (untracked) {
        QFile::remove(QDir(dir).filePath(path));
    } else {
        QString err;
        if (!runGitCapture(dir, {"restore", "--", path}, nullptr, &err) &&
            !runGitCapture(dir, {"checkout", "--", path}, nullptr, nullptr))
            QMessageBox::warning(this, "Discard changes",
                                 err.isEmpty() ? "git restore failed." : err);
    }
    refreshSourceControl();
}

void MainWindow::scmStageAll()
{
    const QString dir = repoGitDir();
    QString err;
    if (!runGitCapture(dir, {"add", "-A"}, nullptr, &err))
        QMessageBox::warning(this, "Stage all", err.isEmpty() ? "git add failed." : err);
    refreshSourceControl();
}

void MainWindow::scmUnstageAll()
{
    const QString dir = repoGitDir();
    runGitCapture(dir, {"reset", "-q"}, nullptr, nullptr);
    refreshSourceControl();
}

void MainWindow::scmDiscardAll()
{
    if (QMessageBox::warning(
            this, "Discard all changes",
            "Discard ALL uncommitted changes, including untracked files? "
            "This cannot be undone.",
            QMessageBox::Discard | QMessageBox::Cancel) != QMessageBox::Discard)
        return;
    const QString dir = repoGitDir();
    runGitCapture(dir, {"restore", "--", "."}, nullptr, nullptr);
    runGitCapture(dir, {"clean", "-fd"}, nullptr, nullptr);
    refreshSourceControl();
}

bool MainWindow::performScmCommit()
{
    const QString dir = repoGitDir();
    if (dir.isEmpty() || !repoHasWorkingTree())
        return false;
    const QString msg = m_scmMessage ? m_scmMessage->text().trimmed() : QString();
    if (msg.isEmpty()) {
        QMessageBox::information(this, "Commit", "Enter a commit message first.");
        return false;
    }
    QByteArray staged;
    runGitCapture(dir, {"diff", "--cached", "--name-only"}, &staged, nullptr);
    if (staged.trimmed().isEmpty()) {
        if (QMessageBox::question(
                this, "Commit",
                "Nothing is staged. Stage all changes and commit?") != QMessageBox::Yes)
            return false;
        QString err;
        if (!runGitCapture(dir, {"add", "-A"}, nullptr, &err)) {
            QMessageBox::warning(this, "Commit", err.isEmpty() ? "git add failed." : err);
            return false;
        }
    }
    QString err;
    if (!runGitCapture(dir, {"commit", "-m", msg}, nullptr, &err)) {
        QMessageBox::warning(this, "Commit",
                             err.isEmpty() ? "git commit failed." : err.left(300));
        return false;
    }
    if (m_scmMessage)
        m_scmMessage->clear();
    logSystem(QStringLiteral("Committed: %1").arg(msg.section('\n', 0, 0)));
    loadCommits(); // refresh history + the working-changes panel
    if (m_repoDetailIndex >= 0)
        propagateRepoUpdate(m_repoDetailIndex);
    return true;
}

void MainWindow::scmCommit()
{
    performScmCommit();
}

void MainWindow::scmCommitAndPush()
{
    // Only push once the commit actually lands; performScmCommit() surfaces any
    // failure (empty message, git error) itself. pushCurrentRepoUpstream() then
    // publishes to the served network mirror or pushes to the upstream branch,
    // matching the "Publish N" button's behaviour.
    if (performScmCommit())
        pushCurrentRepoUpstream();
}

void MainWindow::scmStageAllCommitAndPush()
{
    const QString dir = repoGitDir();
    if (dir.isEmpty() || !repoHasWorkingTree())
        return;
    // Check the message before staging, so a missing one doesn't leave everything
    // staged for nothing (performScmCommit re-checks once the commit runs).
    if (!m_scmMessage || m_scmMessage->text().trimmed().isEmpty()) {
        QMessageBox::information(this, "Commit", "Enter a commit message first.");
        return;
    }
    QString err;
    if (!runGitCapture(dir, {"add", "-A"}, nullptr, &err)) {
        QMessageBox::warning(this, "Stage all",
                             err.isEmpty() ? "git add failed." : err);
        return;
    }
    refreshSourceControl();
    if (performScmCommit())
        pushCurrentRepoUpstream();
}

QString MainWindow::scmContextDiff() const
{
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return QString();

    // Duration scope: 0 = current uncommitted edits only, 1 = since an hour ago,
    // 2 = since midnight. For a window we diff the working tree against the newest
    // commit older than the cutoff, which captures everything committed within the
    // window plus the current uncommitted edits in one diff.
    const int dur = m_scmGenDuration ? m_scmGenDuration->currentIndex() : 0;
    QByteArray diff;
    if (dur == 1 || dur == 2) {
        const QString cutoff =
            dur == 1 ? QStringLiteral("1 hour ago") : QStringLiteral("midnight");
        QByteArray revOut;
        runGitCapture(dir, {"rev-list", "-1", "--before=" + cutoff, "HEAD"}, &revOut,
                      nullptr);
        const QString rev = QString::fromUtf8(revOut).trimmed();
        if (!rev.isEmpty())
            diff = gitCaptureStdout(dir, {"diff", rev});
    }
    // Current changes, or a fallback when nothing was committed within the window.
    if (diff.trimmed().isEmpty()) {
        QByteArray staged = gitCaptureStdout(dir, {"diff", "--cached"});
        diff = staged.trimmed().isEmpty() ? gitCaptureStdout(dir, {"diff"}) : staged;
    }
    QString text = QString::fromUtf8(diff);
    // Note any untracked files, whose contents don't appear in `git diff`.
    QByteArray untracked;
    runGitCapture(dir, {"ls-files", "--others", "--exclude-standard"}, &untracked,
                  nullptr);
    const QString others = QString::fromUtf8(untracked).trimmed();
    if (!others.isEmpty())
        text += QStringLiteral("\n\n# New (untracked) files:\n") + others;
    // Cap for token cost; the model only needs a representative slice.
    constexpr int kCap = 12000;
    if (text.size() > kCap)
        text = text.left(kCap) + QStringLiteral("\n... [diff truncated]");
    return text;
}

QStringList MainWindow::scmDiffScopeArgs() const
{
    const QString dir = repoGitDir();
    const int dur = m_scmGenDuration ? m_scmGenDuration->currentIndex() : 0;
    if (!dir.isEmpty() && (dur == 1 || dur == 2)) {
        const QString cutoff =
            dur == 1 ? QStringLiteral("1 hour ago") : QStringLiteral("midnight");
        QByteArray revOut;
        runGitCapture(dir, {"rev-list", "-1", "--before=" + cutoff, "HEAD"}, &revOut,
                      nullptr);
        const QString rev = QString::fromUtf8(revOut).trimmed();
        if (!rev.isEmpty())
            return {rev};
    }
    QByteArray staged;
    if (!dir.isEmpty())
        runGitCapture(dir, {"diff", "--cached", "--name-only"}, &staged, nullptr);
    if (!staged.trimmed().isEmpty())
        return {QStringLiteral("--cached")};
    return {};
}

// A local, model-free commit-message drafter. It parses the diff to find the
// symbols that changed — types/functions added or removed (keyword-led definitions
// in any language, plus C++ header declarations) and the functions whose bodies
// changed (from git's hunk-header context) — drops generic/internal names, then
// builds the subject from the single most significant symbol, humanizing its
// camelCase into words ("scmHeuristicCommitMessage" -> "scm heuristic commit
// message"). The conventional type comes from the file kinds, the branch name, the
// change shape and a bug-fix keyword scan. Not as good as the AI models, but
// instant, private and free — and far more specific than "update N files"; the
// result lands in the editable message field. `variant` > 0 rotates the symbol
// choice and verb wording so re-clicking Generate offers alternative drafts.
QString MainWindow::scmHeuristicCommitMessage(int variant) const
{
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return QString();
    const QStringList scope = scmDiffScopeArgs();

    struct Change {
        QChar status;
        QString path;    // new path (for renames)
        QString oldPath; // only set for renames
    };
    QList<Change> changes;
    const QString ns = QString::fromUtf8(gitCaptureStdout(
        dir, QStringList{"diff"} + scope + QStringList{"--name-status"}));
    for (const QString &row : ns.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = row.split(QLatin1Char('\t'), Qt::SkipEmptyParts);
        if (f.isEmpty())
            continue;
        const QChar st = f.first().isEmpty() ? QChar('M') : f.first().at(0);
        Change c{st, f.last(), QString()};
        if ((st == QLatin1Char('R') || st == QLatin1Char('C')) && f.size() >= 3)
            c.oldPath = f.at(1);
        changes.append(c);
    }
    // Untracked files only surface under the plain working-tree scope.
    if (scope.isEmpty()) {
        const QString u = QString::fromUtf8(gitCaptureStdout(
            dir, {"ls-files", "--others", "--exclude-standard"}));
        for (const QString &p : u.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            changes.append({QChar('A'), p.trimmed(), QString()});
    }
    if (changes.isEmpty())
        return QString();

    // Per-path category, used to pick a conventional type when the whole change
    // set is one flavour.
    auto category = [](const QString &path) -> QString {
        const QString p = path.toLower();
        const QString base = p.section(QLatin1Char('/'), -1);
        if (p.contains(QLatin1String(".github/workflows/")))
            return QStringLiteral("ci");
        if (base.endsWith(QLatin1String(".md")) || base.endsWith(QLatin1String(".rst"))
            || p.contains(QLatin1String("/docs/")) || p.startsWith(QLatin1String("docs/"))
            || base.startsWith(QLatin1String("readme"))
            || base.startsWith(QLatin1String("changelog")))
            return QStringLiteral("docs");
        if (p.contains(QLatin1String("/tests/")) || p.contains(QLatin1String("/test/"))
            || base.startsWith(QLatin1String("test_"))
            || base.contains(QLatin1String("_test."))
            || base.contains(QLatin1String(".test."))
            || base.contains(QLatin1String("spec.")))
            return QStringLiteral("test");
        if (base == QLatin1String("cmakelists.txt") || base.endsWith(QLatin1String(".cmake"))
            || base == QLatin1String("makefile") || base == QLatin1String("dockerfile")
            || base == QLatin1String("package.json") || base.endsWith(QLatin1String(".lock"))
            || base.endsWith(QLatin1String(".yml")) || base.endsWith(QLatin1String(".yaml"))
            || base.endsWith(QLatin1String(".toml")) || base == QLatin1String(".gitignore"))
            return QStringLiteral("build");
        return QStringLiteral("code");
    };

    int added = 0, removed = 0, renamed = 0;
    QString commonCat;
    bool uniformCat = true;
    QStringList commonDir;
    bool firstDir = true;
    for (const Change &c : changes) {
        if (c.status == QLatin1Char('A')) ++added;
        else if (c.status == QLatin1Char('D')) ++removed;
        else if (c.status == QLatin1Char('R')) ++renamed;

        const QString cat = category(c.path);
        if (commonCat.isEmpty()) commonCat = cat;
        else if (commonCat != cat) uniformCat = false;

        const int slash = c.path.lastIndexOf(QLatin1Char('/'));
        const QStringList parts =
            (slash >= 0 ? c.path.left(slash) : QString()).split(QLatin1Char('/'),
                                                                Qt::SkipEmptyParts);
        if (firstDir) { commonDir = parts; firstDir = false; }
        else {
            int k = 0;
            while (k < commonDir.size() && k < parts.size() && commonDir[k] == parts[k])
                ++k;
            commonDir = commonDir.mid(0, k);
        }
    }

    // ---- content analysis: walk the patch and classify which functions/types were
    // added, removed, or had their bodies changed. Naming the actual symbols is what
    // makes the message describe the change instead of just counting files. ----
    QHash<QString, int> addedType, addedFn, removedType, removedFn, touchedFn;
    QString addedText; // added code lines, for the bug-fix keyword scan
    {
        const QString patch = QString::fromUtf8(gitCaptureStdout(
            dir, QStringList{"diff"} + scope + QStringList{"--unified=0"}));
        // Enclosing function from a hunk-header context: the identifier just before
        // the first '(' (after any Class:: qualifier).
        auto funcFromContext = [](const QString &ctx) -> QString {
            const int paren = ctx.indexOf(QLatin1Char('('));
            if (paren < 0)
                return QString();
            static const QRegularExpression tail(
                QStringLiteral("([A-Za-z_][A-Za-z0-9_]*)\\s*$"));
            const auto m = tail.match(ctx.left(paren));
            if (!m.hasMatch())
                return QString();
            static const QSet<QString> kw = {
                QStringLiteral("if"),    QStringLiteral("for"),
                QStringLiteral("while"), QStringLiteral("switch"),
                QStringLiteral("catch"), QStringLiteral("return"),
                QStringLiteral("sizeof")};
            return kw.contains(m.captured(1)) ? QString() : m.captured(1);
        };
        // Definitions introduced/removed on one line. Keyword-led forms work for any
        // language; in a header a "<type> name(args);" declaration counts too.
        // All anchored at the start of the (comment-stripped) line: a real
        // definition begins the statement, whereas prose mentioning "function" or
        // "struct" inside a comment does not.
        static const QRegularExpression typeDef(QStringLiteral(
            "^(?:typedef\\s+)?(?:class|struct|enum)\\s+(?:class\\s+)?([A-Za-z_]\\w*)"));
        static const QRegularExpression langFn(QStringLiteral(
            "^(?:export\\s+|public\\s+|private\\s+|pub\\s+|async\\s+|static\\s+)*"
            "(?:def|func|fn|function|sub)\\s+([A-Za-z_]\\w*)"));
        static const QRegularExpression headerDecl(QStringLiteral(
            "^[A-Za-z_][\\w:<>,*&\\s]*\\s[*&]*([A-Za-z_]\\w*)\\s*\\([^;{]*\\)\\s*"
            "(?:const|override|noexcept|final|=\\s*\\w+|\\s)*;\\s*$"));
        auto scanDefs = [&](const QString &content, bool header,
                            QHash<QString, int> &types, QHash<QString, int> &fns) {
            QString t = content.trimmed();
            // Skip comment and preprocessor lines so their prose can't pose as code.
            if (t.isEmpty() || t.startsWith(QLatin1String("//"))
                || t.startsWith(QLatin1Char('*')) || t.startsWith(QLatin1String("/*"))
                || t.startsWith(QLatin1Char('#')))
                return;
            const int cmt = t.indexOf(QLatin1String("//"));
            if (cmt > 0)
                t = t.left(cmt).trimmed();
            const auto mt = typeDef.match(t);
            if (mt.hasMatch()) { ++types[mt.captured(1)]; return; }
            const auto ml = langFn.match(t);
            if (ml.hasMatch()) { ++fns[ml.captured(1)]; return; }
            if (header) {
                const auto mh = headerDecl.match(t);
                if (mh.hasMatch())
                    ++fns[mh.captured(1)];
            }
        };

        QString file;
        bool header = false;
        int seen = 0;
        const QStringList lines = patch.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (++seen > 40000) // bound the work on a very large diff
                break;
            if (line.startsWith(QLatin1String("+++ "))) {
                file = line.mid(4).trimmed();
                if (file.startsWith(QLatin1String("b/")))
                    file = file.mid(2);
                const QString lower = file.toLower();
                header = lower.endsWith(QLatin1String(".h"))
                         || lower.endsWith(QLatin1String(".hpp"))
                         || lower.endsWith(QLatin1String(".hh"))
                         || lower.endsWith(QLatin1String(".hxx"));
                continue;
            }
            if (line.startsWith(QLatin1String("--- ")))
                continue;
            if (line.startsWith(QLatin1String("@@"))) {
                const int second = line.indexOf(QLatin1String("@@"), 2);
                if (second >= 0) {
                    const QString fn = funcFromContext(line.mid(second + 2));
                    if (!fn.isEmpty())
                        ++touchedFn[fn];
                }
                continue;
            }
            if (line.startsWith(QLatin1Char('+'))) {
                const QString content = line.mid(1);
                scanDefs(content, header, addedType, addedFn);
                addedText += content.toLower();
                addedText += QLatin1Char('\n');
            } else if (line.startsWith(QLatin1Char('-'))) {
                scanDefs(line.mid(1), header, removedType, removedFn);
            }
        }
    }

    // Split camelCase / snake_case / digit runs into lowercase words — a symbol's
    // own name is the developer's description of what it does, so "deletePullAndBranch"
    // reads back as "delete pull and branch".
    auto humanize = [](const QString &name) -> QString {
        QString out;
        for (int i = 0; i < name.size(); ++i) {
            const QChar c = name.at(i);
            if (c == QLatin1Char('_') || c == QLatin1Char('-')) {
                if (!out.isEmpty() && !out.endsWith(QLatin1Char(' ')))
                    out += QLatin1Char(' ');
                continue;
            }
            const bool prevLower = i > 0 && name.at(i - 1).isLower();
            const bool prevUpper = i > 0 && name.at(i - 1).isUpper();
            const bool nextLower = i + 1 < name.size() && name.at(i + 1).isLower();
            const bool boundary = (c.isUpper() && prevLower)              // fooBar
                                  || (c.isUpper() && prevUpper && nextLower) // HTMLParser
                                  || (c.isDigit() && i > 0 && !name.at(i - 1).isDigit());
            if (boundary && !out.isEmpty() && !out.endsWith(QLatin1Char(' ')))
                out += QLatin1Char(' ');
            out += c.toLower();
        }
        return out.simplified();
    };

    // Generic / internal names that make a poor headline.
    static const QSet<QString> kGeneric = {
        QStringLiteral("change"),  QStringLiteral("data"),    QStringLiteral("item"),
        QStringLiteral("info"),    QStringLiteral("helper"),  QStringLiteral("impl"),
        QStringLiteral("result"),  QStringLiteral("options"), QStringLiteral("option"),
        QStringLiteral("config"),  QStringLiteral("context"), QStringLiteral("entry"),
        QStringLiteral("node"),    QStringLiteral("pair"),    QStringLiteral("tmp"),
        QStringLiteral("temp"),    QStringLiteral("foo"),     QStringLiteral("bar"),
        QStringLiteral("base"),    QStringLiteral("value"),   QStringLiteral("object"),
        QStringLiteral("list"),    QStringLiteral("map"),     QStringLiteral("util"),
        QStringLiteral("utils"),   QStringLiteral("main"),    QStringLiteral("init"),
        QStringLiteral("state"),   QStringLiteral("params"),  QStringLiteral("args"),
        QStringLiteral("type"),    QStringLiteral("types"),   QStringLiteral("handler")};

    // Rank one or more (freq, opposite-side, bonus) buckets together by descriptive
    // merit, dropping the opposite side and generic names. Merging into a single
    // sort (rather than concatenating) lets a well-named function outrank a terse
    // internal struct, while `bonus` still tilts ties toward new types.
    struct Bucket {
        const QHash<QString, int> *freq;
        const QHash<QString, int> *exclude;
        int bonus;
    };
    auto rankMerged = [&](std::initializer_list<Bucket> buckets) -> QStringList {
        struct Cand { QString name; int score; };
        QList<Cand> cs;
        for (const Bucket &b : buckets)
            for (auto i = b.freq->cbegin(); i != b.freq->cend(); ++i) {
                const QString &n = i.key();
                if (b.exclude->contains(n) || kGeneric.contains(n.toLower())
                    || n.size() < 4)
                    continue;
                const int words = humanize(n).count(QLatin1Char(' ')) + 1;
                cs.append({n, b.bonus + qMin(words, 4) + qMin(i.value(), 3)});
            }
        std::sort(cs.begin(), cs.end(), [](const Cand &a, const Cand &b) {
            if (a.score != b.score) return a.score > b.score;
            return a.name < b.name;
        });
        QStringList out;
        for (const Cand &c : cs) out << c.name;
        return out;
    };
    auto fileStems = [&](QChar status) -> QStringList {
        QStringList out;
        for (const Change &c : changes)
            if (c.status == status) {
                QString b = c.path.section(QLatin1Char('/'), -1);
                const int dot = b.lastIndexOf(QLatin1Char('.'));
                if (dot > 0) b = b.left(dot);
                if (!kGeneric.contains(b.toLower()))
                    out << b;
            }
        return out;
    };

    const QHash<QString, int> none;
    // New symbols, ranked together (a small type tilt breaks ties toward classes,
    // but descriptiveness wins — so a one-line helper struct can't beat a
    // well-named function). Fall back to whole new file names.
    QStringList newNames =
        rankMerged({{&addedType, &removedType, 2}, {&addedFn, &removedFn, 1}});
    QStringList goneNames =
        rankMerged({{&removedType, &addedType, 2}, {&removedFn, &addedFn, 1}});
    if (newNames.isEmpty() && added > 0)
        newNames = fileStems(QChar('A'));
    if (goneNames.isEmpty() && removed > 0)
        goneNames = fileStems(QChar('D'));
    // Functions whose bodies changed (not net added or removed).
    QHash<QString, int> changedFreq = touchedFn;
    for (const QString &n : addedFn.keys()) changedFreq.remove(n);
    for (const QString &n : addedType.keys()) changedFreq.remove(n);
    for (const QString &n : removedFn.keys()) changedFreq.remove(n);
    for (const QString &n : removedType.keys()) changedFreq.remove(n);
    const QStringList chgNames = rankMerged({{&changedFreq, &none, 0}});

    // fix-signal: words in the added code that strongly suggest a bug fix.
    static const QRegularExpression fixWords(QStringLiteral(
        "\\b(fix|bug|crash|freeze|hang|leak|deadlock|segfault|overflow|regression|"
        "guard|workaround|race)\\b"));
    const bool fixy = fixWords.match(addedText).hasMatch();

    // ---- conventional type ----
    QString type;
    if (uniformCat && commonCat != QLatin1String("code"))
        type = commonCat;
    if (type.isEmpty()) {
        const QString branch = QString::fromUtf8(gitCaptureStdout(
            dir, {"rev-parse", "--abbrev-ref", "HEAD"})).trimmed().toLower();
        if (branch.contains(QLatin1String("fix")) || branch.contains(QLatin1String("bug"))
            || branch.contains(QLatin1String("hotfix")))
            type = QStringLiteral("fix");
        else if (branch.contains(QLatin1String("feat")))
            type = QStringLiteral("feat");
        else if (branch.contains(QLatin1String("refactor")))
            type = QStringLiteral("refactor");
    }
    if (type.isEmpty()) {
        if (!newNames.isEmpty() && goneNames.isEmpty() && removed == 0 && renamed == 0)
            type = QStringLiteral("feat");
        else if (fixy)
            type = QStringLiteral("fix");
        else if (!chgNames.isEmpty() || !goneNames.isEmpty() || !newNames.isEmpty()
                 || removed > 0 || renamed > 0)
            type = QStringLiteral("refactor");
        else
            type = QStringLiteral("chore");
    }

    // ---- subject: name the headline symbol(s), humanized. A second name is added
    // only when it's a clearly different theme (different first word), so we never
    // emit "scm X and scm Y" or a vague "and N more" tail. ----
    // On a re-click (variant > 0) rotate the pool so a different symbol leads —
    // but only among the strongest few, so cycling never digs down to the noisy
    // low-ranked tail (terse internal structs, etc.).
    auto rotate = [&](QStringList names) -> QStringList {
        if (names.size() > 4)
            names = names.mid(0, 4);
        if (variant > 0 && names.size() > 1) {
            const int off = variant % names.size();
            names = names.mid(off) + names.mid(0, off);
        }
        return names;
    };
    auto headline = [&](const QStringList &all) -> QStringList {
        const QStringList names = rotate(all);
        QStringList out;
        for (const QString &n : names) {
            if (out.isEmpty()) { out << n; continue; }
            if (humanize(n).section(QLatin1Char(' '), 0, 0)
                != humanize(out.first()).section(QLatin1Char(' '), 0, 0)) {
                out << n;
                break;
            }
        }
        return out;
    };

    QString subject;
    QString verb;     // "" means let the conventional type carry the verb
    QStringList pick; // the chosen symbol name(s)
    const bool renameOne =
        changes.size() == 1 && renamed == 1 && !changes.first().oldPath.isEmpty();
    if (renameOne) {
        subject = QStringLiteral("rename %1 to %2")
                      .arg(changes.first().oldPath.section(QLatin1Char('/'), -1),
                           changes.first().path.section(QLatin1Char('/'), -1));
    } else if (!newNames.isEmpty()) {
        verb = QStringLiteral("add");
        pick = headline(newNames);
    } else if (!goneNames.isEmpty()) {
        verb = QStringLiteral("remove");
        pick = headline(goneNames);
    } else if (!chgNames.isEmpty()) {
        pick = headline(chgNames);
    }
    // Vary the wording on a re-click: a synonym for add/remove, or a light verb in
    // front of a body-change subject (which normally lets the type carry the verb).
    if (variant > 0 && !renameOne) {
        if (verb == QLatin1String("add")) {
            static const QStringList syn = {QStringLiteral("add"),
                QStringLiteral("introduce"), QStringLiteral("implement"),
                QStringLiteral("create"), QStringLiteral("wire up")};
            verb = syn.at(variant % syn.size());
        } else if (verb == QLatin1String("remove")) {
            static const QStringList syn = {QStringLiteral("remove"),
                QStringLiteral("drop"), QStringLiteral("delete"),
                QStringLiteral("strip out")};
            verb = syn.at(variant % syn.size());
        } else if (verb.isEmpty() && !pick.isEmpty()) {
            static const QStringList syn = {QString(), QStringLiteral("tweak"),
                QStringLiteral("rework"), QStringLiteral("refine"),
                QStringLiteral("revise"), QStringLiteral("adjust")};
            verb = syn.at(variant % syn.size());
        }
    }
    auto compose = [&](int maxNames) -> QString {
        QStringList words;
        for (const QString &n : pick.mid(0, maxNames))
            words << humanize(n);
        QString ph;
        if (words.size() >= 2) {
            // Use a comma when a name already contains "and" to avoid "X and Y and Z".
            const QString sep = (words[0].contains(QStringLiteral(" and "))
                                 || words[1].contains(QStringLiteral(" and ")))
                                    ? QStringLiteral(", ")
                                    : QStringLiteral(" and ");
            ph = words[0] + sep + words[1];
        } else {
            ph = words.value(0);
        }
        if (ph.isEmpty())
            return QString();
        return verb.isEmpty() ? ph : verb + QLatin1Char(' ') + ph;
    };
    if (subject.isEmpty() && !pick.isEmpty())
        subject = compose(2);
    if (subject.isEmpty()) {
        // Nothing nameable (e.g. data/config only): fall back to a file summary.
        const QString fv = added == changes.size()    ? QStringLiteral("add")
                           : removed == changes.size() ? QStringLiteral("remove")
                           : renamed == changes.size() ? QStringLiteral("rename")
                                                       : QStringLiteral("update");
        subject = changes.size() == 1
                      ? QStringLiteral("%1 %2").arg(
                            fv, changes.first().path.section(QLatin1Char('/'), -1))
                      : QStringLiteral("%1 %2 files").arg(fv).arg(changes.size());
    }

    // Optional scope: the common directory, unless it's a generic container.
    static const QSet<QString> kGenericScope = {
        QStringLiteral("src"),     QStringLiteral("lib"),  QStringLiteral("source"),
        QStringLiteral("sources"), QStringLiteral("app"),  QStringLiteral("code"),
        QStringLiteral("include"), QStringLiteral("dist")};
    QString scopeName = commonDir.isEmpty() ? QString() : commonDir.last();
    if (kGenericScope.contains(scopeName.toLower()))
        scopeName.clear();

    auto assemble = [&](const QString &subj) {
        QString m = type;
        if (!scopeName.isEmpty() && scopeName.size() <= 20)
            m += QStringLiteral("(%1)").arg(scopeName);
        return m + QStringLiteral(": ") + subj;
    };
    QString msg = assemble(subject);
    // If naming two themes overran, fall back to just the first.
    if (msg.size() > 72 && pick.size() >= 2)
        msg = assemble(compose(1));
    return msg;
}

void MainWindow::autoFillScmMessage()
{
    // Only the free, local, instant drafter runs on its own — never the paid AI
    // models, and never over a message the user has started typing.
    if (m_scmGenerating || !m_scmMessage || !m_scmGenModel
        || m_scmGenModel->currentData().toInt() != -1
        || !m_scmMessage->text().trimmed().isEmpty())
        return;
    const QString msg = scmHeuristicCommitMessage(0); // 0 = the deterministic best
    if (msg.isEmpty())
        return;
    m_scmHeuristicVariant = 0; // a fresh auto-fill restarts the "vary on click" cycle
    m_scmMessage->setText(msg);
    if (m_scmGenStatus)
        m_scmGenStatus->setText(QString::fromUtf8(
            "%1 chars \xc2\xb7 on-device, auto \xc2\xb7 \xE2\x86\xBB click to vary")
                                    .arg(msg.size()));
}

void MainWindow::generateScmMessage()
{
    if (m_scmGenerating)
        return;
    const QString diff = scmContextDiff();
    if (diff.trimmed().isEmpty()) {
        if (m_scmGenStatus)
            m_scmGenStatus->setText("No changes to describe.");
        return;
    }
    const int mi = m_scmGenModel ? m_scmGenModel->currentData().toInt() : 0;
    if (mi < 0) { // On-device (no AI): derive the message locally from the diff.
        // Each explicit click advances the variant so re-clicking "refreshes" to an
        // alternative phrasing/symbol choice (auto-fill always uses variant 0).
        const QString msg = scmHeuristicCommitMessage(++m_scmHeuristicVariant);
        if (msg.isEmpty()) {
            m_scmHeuristicVariant = 0;
            if (m_scmGenStatus)
                m_scmGenStatus->setText("No changes to describe.");
            return;
        }
        if (m_scmMessage)
            m_scmMessage->setText(msg);
        const int n = m_scmMessage ? m_scmMessage->text().size() : msg.size();
        if (m_scmGenStatus)
            m_scmGenStatus->setText(
                QString::fromUtf8("%1 chars \xc2\xb7 on-device, no cost \xc2\xb7 \xE2\x86\xBB click to vary").arg(n));
        return;
    }
    if (mi >= kScmAiModelCount)
        return;
    const ScmAiModel model = kScmAiModels[mi];
    const bool isCommit = !(m_scmGenKind && m_scmGenKind->currentIndex() == 1);
    const int outTok = isCommit ? 120 : 280;

    const QString system =
        isCommit ? QStringLiteral("You write concise, conventional git commit "
                                  "messages.")
                 : QStringLiteral("You write short, engaging X (Twitter) posts "
                                  "for developers.");
    const QString task =
        isCommit
            ? QStringLiteral(
                  "Write a single-line git commit message for the following "
                  "changes: a short imperative subject (max ~70 chars), no body. "
                  "Output ONLY that one line, with no backticks or commentary."
                  "\n\nDiff:\n%1")
                  .arg(diff)
            : QStringLiteral(
                  "Write a single engaging X (Twitter) post, at most 280 "
                  "characters, announcing these code changes to developer "
                  "followers. At most two relevant hashtags. Output ONLY the post "
                  "text.\n\nChanges:\n%1")
                  .arg(diff);

    const bool claude =
        QString::fromLatin1(model.provider) == QLatin1String("claude");
    const QString apiKey =
        claude ? QSettings().value(kClaudeApiKeySetting).toString().trimmed()
               : QSettings().value(kCodexApiKeySetting).toString().trimmed();
    if (apiKey.isEmpty()) {
        if (m_scmGenStatus)
            m_scmGenStatus->setText(claude ? "Add a Claude API key in Settings."
                                           : "Add an OpenAI API key in Settings.");
        return;
    }

    QNetworkReply *reply = nullptr;
    if (claude) {
        QJsonObject payload;
        payload.insert("model", QString::fromLatin1(model.id));
        payload.insert("max_tokens", outTok);
        QJsonArray messages;
        QJsonObject um;
        um.insert("role", "user");
        um.insert("content", system + "\n\n" + task);
        messages.append(um);
        payload.insert("messages", messages);
        QNetworkRequest req(QUrl("https://api.anthropic.com/v1/messages"));
        req.setRawHeader("x-api-key", apiKey.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_networkAccess->post(
            req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    } else {
        QJsonObject payload;
        payload.insert("model", QString::fromLatin1(model.id));
        payload.insert("instructions", system);
        payload.insert("input", task);
        payload.insert("max_output_tokens", outTok);
        QNetworkRequest req =
            openAiRequest(QUrl("https://api.openai.com/v1/responses"), apiKey);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_networkAccess->post(
            req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }

    m_scmGenerating = true;
    if (m_scmGenerateButton) {
        m_scmGenerateButton->setEnabled(false);
        m_scmGenerateButton->setText(QString::fromUtf8("Generating\xE2\x80\xA6"));
    }
    if (m_scmGenStatus)
        m_scmGenStatus->setText(QString::fromUtf8("Asking %1\xE2\x80\xA6")
                                    .arg(QString::fromLatin1(model.label)));

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, claude, model, isCommit] {
                const QByteArray body = reply->readAll();
                reply->deleteLater();
                m_scmGenerating = false;
                if (m_scmGenerateButton) {
                    m_scmGenerateButton->setEnabled(true);
                    m_scmGenerateButton->setText(
                        QString::fromUtf8("\xE2\x9C\xA8 Generate"));
                }
                if (reply->error() != QNetworkReply::NoError) {
                    if (m_scmGenStatus)
                        m_scmGenStatus->setText("Failed: " +
                                                apiErrorSummary(reply, body));
                    return;
                }
                const QJsonObject obj = QJsonDocument::fromJson(body).object();
                QString text;
                double cost = 0.0;
                qint64 inTok = 0, outTokActual = 0;
                if (claude) {
                    for (const QJsonValue &v : obj.value("content").toArray()) {
                        const QJsonObject o = v.toObject();
                        if (o.value("type").toString() == QLatin1String("text"))
                            text += o.value("text").toString();
                    }
                    const QJsonObject usage = obj.value("usage").toObject();
                    inTok = usage.value("input_tokens").toInt();
                    outTokActual = usage.value("output_tokens").toInt();
                    cost = inTok / 1e6 * model.inPerM +
                           outTokActual / 1e6 * model.outPerM;
                } else {
                    text = openAiResponseText(obj);
                    cost = openAiAskCostUsd(obj, &inTok, &outTokActual);
                }
                text = text.trimmed();
                if (text.isEmpty()) {
                    if (m_scmGenStatus)
                        m_scmGenStatus->setText("Empty response.");
                    return;
                }
                // The message field is single-line, so collapse any stray
                // newlines before showing it (the clipboard keeps the original).
                if (m_scmMessage) {
                    QString oneLine = text;
                    oneLine.replace(QLatin1Char('\n'), QLatin1Char(' '));
                    m_scmMessage->setText(oneLine.simplified());
                }
                // Char count first (what the user asked to see), then the cost.
                const int n = m_scmMessage ? m_scmMessage->text().size()
                                           : text.size();
                QString line = isCommit ? QStringLiteral("%1 chars").arg(n)
                                        : QStringLiteral("%1/280").arg(n);
                line += QStringLiteral(" \xC2\xB7 $%1 \xC2\xB7 %2 in / %3 out")
                            .arg(QString::number(cost, 'f', 4))
                            .arg(inTok)
                            .arg(outTokActual);
                // An X post also goes to the clipboard so it's one paste from being
                // posted; keep the original (possibly multi-line) text there.
                if (!isCommit) {
                    QApplication::clipboard()->setText(text);
                    line += QStringLiteral(", copied");
                }
                if (m_scmGenStatus)
                    m_scmGenStatus->setText(line);
            });
}

QWidget *MainWindow::buildRepoSecurityTab()
{
    auto *page = new QWidget;
    page->setObjectName("mainContent");

    auto *scroll = new QScrollArea;
    scroll->setObjectName("mainContent");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    content->setObjectName("insightsPage");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);

    auto *heading = new QLabel("Security");
    heading->setObjectName("securityQualityTitle");
    heading->setProperty("class", "channelTitle");
    heading->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;"));
    auto *subtitle = new QLabel(
        "Local security evidence from this node. External advisory matching is "
        "not part of the MVP; quality metrics live on the Quality tab.");
    subtitle->setObjectName("statusLine");
    subtitle->setWordWrap(true);

    m_securityRefreshButton = new QPushButton("Refresh");
    m_securityRefreshButton->setObjectName("ghostButton");
    m_securityRefreshButton->setProperty("buttonSize", "sm");
    m_securityRefreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_securityRefreshButton, "sync", 16);
    connect(m_securityRefreshButton, &QPushButton::clicked, this,
            &MainWindow::refreshRepoSecurity);

    auto *headingCol = new QVBoxLayout;
    headingCol->setContentsMargins(0, 0, 0, 0);
    headingCol->setSpacing(3);
    headingCol->addWidget(heading);
    headingCol->addWidget(subtitle);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);
    headerRow->addLayout(headingCol, 1);
    headerRow->addWidget(m_securityRefreshButton, 0, Qt::AlignTop);
    layout->addLayout(headerRow);

    m_securitySummary = new QLabel;
    m_securitySummary->setObjectName("insightsCard");
    m_securitySummary->setTextFormat(Qt::RichText);
    m_securitySummary->setWordWrap(true);
    m_securitySummary->setMinimumHeight(92);
    layout->addWidget(m_securitySummary);

    auto *signalsLabel = new QLabel("SIGNALS");
    signalsLabel->setObjectName("sectionLabel");
    layout->addWidget(signalsLabel);

    m_securitySignalsPanel = new QWidget;
    m_securitySignalsGrid = new QGridLayout(m_securitySignalsPanel);
    m_securitySignalsGrid->setContentsMargins(0, 0, 0, 0);
    m_securitySignalsGrid->setSpacing(10);
    layout->addWidget(m_securitySignalsPanel);

    auto *findingsLabel = new QLabel("FINDINGS");
    findingsLabel->setObjectName("sectionLabel");
    layout->addWidget(findingsLabel);

    m_securityFindingsTable = new QTableWidget(0, 4);
    installColumnHeaderMenu(m_securityFindingsTable); // 3-dots per-column menu (issue #318)
    m_securityFindingsTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_securityFindingsTable);
    m_securityFindingsTable->setHorizontalHeaderLabels(
        {"Severity", "Category", "Finding", "Action"});
    m_securityFindingsTable->verticalHeader()->setVisible(false);
    m_securityFindingsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_securityFindingsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_securityFindingsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_securityFindingsTable->setShowGrid(false);
    m_securityFindingsTable->setWordWrap(false);
    QHeaderView *header = m_securityFindingsTable->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_securityFindingsTable);
    m_securityFindingsTable->setMinimumHeight(220);
    layout->addWidget(m_securityFindingsTable);
    layout->addStretch();
    connect(m_securityFindingsTable, &QTableWidget::cellClicked,
            this, [this](int row, int /*col*/) {
        auto *item = m_securityFindingsTable->item(row, 2);
        if (!item)
            return;
        const QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty())
            return;
        // Switch to the Code tab (index 0) so the highlighted line is visible;
        // openRepoFileAtLine alone only touches the (currently hidden) files panel.
        if (m_repoDetailTabs && m_repoDetailTabs->button(0))
            m_repoDetailTabs->button(0)->click();
        openRepoFileAtLine(path, item->data(Qt::UserRole + 1).toInt());
    });

    scroll->setWidget(content);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scroll);
    refreshRepoSecurity();
    return page;
}

// Severity -> badge/text colour for the Security and Quality tabs. Mirrors the
// palette used elsewhere (green pass, blue info, amber warning, orange/red severe).
static QString repoSecuritySeverityColor(RepoSecuritySeverity severity)
{
    switch (severity) {
    case RepoSecuritySeverity::Pass:
        return QStringLiteral("#3fb950");
    case RepoSecuritySeverity::Info:
        return QStringLiteral("#58a6ff");
    case RepoSecuritySeverity::Warning:
        return QStringLiteral("#d29922");
    case RepoSecuritySeverity::High:
        return QStringLiteral("#f0883e");
    case RepoSecuritySeverity::Critical:
        return QStringLiteral("#f85149");
    }
    return QStringLiteral("#8b949e");
}

// Render one security signal as an "insightsCard" HTML body: severity-tinted
// title, the summary, and (when present) the longer detail.
static QString repoSecuritySignalHtml(const RepoSecuritySignal &signal)
{
    const QString color = repoSecuritySeverityColor(signal.severity);
    QString html = QStringLiteral(
                       "<div style='font-weight:700; font-size:13px; color:%1'>%2</div>"
                       "<div style='color:#8b949e; font-size:11px; font-weight:600; "
                       "text-transform:uppercase; margin-top:2px'>%3</div>")
                       .arg(color, signal.title.toHtmlEscaped(),
                            RepoSecurity::severityText(signal.severity).toHtmlEscaped());
    if (!signal.summary.isEmpty())
        html += QStringLiteral(
                    "<div style='color:#c9d1d9; font-size:12px; margin-top:6px'>%1</div>")
                    .arg(signal.summary.toHtmlEscaped());
    if (!signal.detail.isEmpty())
        html += QStringLiteral(
                    "<div style='color:#8b949e; font-size:11px; margin-top:4px'>%1</div>")
                    .arg(signal.detail.toHtmlEscaped());
    // Itemised entries (e.g. dependency manifests) render as a list where each
    // path is a "check" link; the card's linkActivated handler opens the file.
    for (const QString &item : signal.items) {
        const QString href = QString::fromUtf8(QUrl::toPercentEncoding(item));
        html += QStringLiteral(
                    "<div style='color:#c9d1d9; font-size:12px; margin-top:4px'>"
                    "&#8226;&nbsp;<a style='color:#58a6ff; text-decoration:none' "
                    "href='manifest:%1'>%2</a></div>")
                    .arg(href, item.toHtmlEscaped());
    }
    return html;
}

// Fill a Security/Quality findings table: one row per finding (or a single
// "Pass" row when there are none). Column 2 carries the path/line payload the
// tables' cellClicked handlers use to open the file.
static void fillRepoFindingsTable(QTableWidget *table,
                                  const QList<RepoSecurityFinding> &findings,
                                  const QString &emptyText)
{
    if (findings.isEmpty()) {
        const int row = table->rowCount();
        table->insertRow(row);
        auto *severity = new QTableWidgetItem("Pass");
        severity->setForeground(QColor(repoSecuritySeverityColor(
            RepoSecuritySeverity::Pass)));
        table->setItem(row, 0, severity);
        table->setItem(row, 1, new QTableWidgetItem("Local scan"));
        table->setItem(row, 2, new QTableWidgetItem(emptyText));
        table->setItem(row, 3, new QTableWidgetItem("-"));
        return;
    }
    for (const RepoSecurityFinding &finding : findings) {
        const int row = table->rowCount();
        table->insertRow(row);
        auto *severity =
            new QTableWidgetItem(RepoSecurity::severityText(finding.severity));
        severity->setForeground(QColor(repoSecuritySeverityColor(finding.severity)));
        auto *category = new QTableWidgetItem(finding.category);
        const QString location =
            finding.path.isEmpty()
                ? QString()
                : QStringLiteral(" (%1%2)")
                      .arg(finding.path,
                           finding.line > 0
                               ? QStringLiteral(":%1").arg(finding.line)
                               : QString());
        auto *detail =
            new QTableWidgetItem(finding.title + QStringLiteral(": ") +
                                 finding.detail + location);
        if (!finding.path.isEmpty()) {
            detail->setData(Qt::UserRole, finding.path);
            detail->setData(Qt::UserRole + 1, finding.line);
            detail->setToolTip(
                QStringLiteral("Click to open %1:%2")
                    .arg(finding.path)
                    .arg(finding.line > 0 ? QString::number(finding.line)
                                          : QStringLiteral("?")));
        }
        auto *action = new QTableWidgetItem(finding.recommendedAction);
        for (QTableWidgetItem *item : {severity, category, action})
            item->setToolTip(item->text());
        table->setItem(row, 0, severity);
        table->setItem(row, 1, category);
        table->setItem(row, 2, detail);
        table->setItem(row, 3, action);
    }
}

// Renders the dependency-scan signal as a rich table widget: one row per
// manifest with dependency-count, outdated-count, vulnerable-count, and a
// per-row "Run scan" button.  Placed full-width (spanning all 4 grid columns)
// below the other signal cards.
static QWidget *buildDepScanCard(const RepoSecuritySignal &signal,
                                  const QList<RepoSecurityFinding> &findings,
                                  const QHash<QString, int> &dependencyCounts,
                                  qint64 generatedAtMs,
                                  const QString &localBase,
                                  QObject *context,
                                  bool scanning,
                                  const QString &scanningPath,
                                  std::function<void(const QString &)> runScan)
{
    auto *card = new QFrame;
    card->setObjectName("insightsCard");
    auto *vlay = new QVBoxLayout(card);
    vlay->setContentsMargins(12, 12, 12, 12);
    vlay->setSpacing(6);

    // Title + severity + last-scanned time in one row
    const QString color = repoSecuritySeverityColor(signal.severity);
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    auto *titleLbl = new QLabel(
        QStringLiteral("<span style='font-weight:700;font-size:13px;color:%1'>%2</span>"
                       "&nbsp;<span style='color:#8b949e;font-size:11px;font-weight:600;"
                       "text-transform:uppercase'>%3</span>")
            .arg(color, signal.title.toHtmlEscaped(),
                 RepoSecurity::severityText(signal.severity).toHtmlEscaped()));
    titleLbl->setTextFormat(Qt::RichText);
    titleRow->addWidget(titleLbl, 1);
    if (scanning) {
        auto *scanningLbl = new QLabel(
            QStringLiteral("<span style='color:#388bfd;font-size:11px;font-weight:600'>"
                           "Scanning&#8230;</span>"));
        scanningLbl->setTextFormat(Qt::RichText);
        titleRow->addWidget(scanningLbl, 0);
    } else if (generatedAtMs > 0) {
        const QString ts = QDateTime::fromMSecsSinceEpoch(generatedAtMs)
                               .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
        auto *timeLbl = new QLabel(
            QStringLiteral("<span style='color:#8b949e;font-size:11px'>Last scanned: %1</span>")
                .arg(ts));
        timeLbl->setTextFormat(Qt::RichText);
        titleRow->addWidget(timeLbl, 0);
    }
    vlay->addLayout(titleRow);

    if (scanning) {
        // Indeterminate (range 0,0) progress bar: Qt animates a marquee chunk on
        // its own timer, giving a genuine moving "scan in progress" indicator
        // rather than a static icon (the scan itself runs off the GUI thread via
        // MainWindow::runRepoDependencyScan, so this can actually animate).
        auto *bar = new QProgressBar;
        bar->setRange(0, 0);
        bar->setTextVisible(false);
        bar->setFixedHeight(4);
        bar->setStyleSheet(
            QStringLiteral("QProgressBar{border:none;border-radius:2px;"
                           "background-color:#30363d;}"
                           "QProgressBar::chunk{border-radius:2px;"
                           "background-color:#388bfd;}"));
        vlay->addWidget(bar);
    }

    if (!signal.summary.isEmpty()) {
        auto *summaryLbl = new QLabel(
            QStringLiteral("<span style='color:#c9d1d9;font-size:12px'>%1</span>")
                .arg(signal.summary.toHtmlEscaped()));
        summaryLbl->setTextFormat(Qt::RichText);
        vlay->addWidget(summaryLbl);
    }

    if (signal.items.isEmpty()) {
        if (!signal.detail.isEmpty()) {
            auto *detailLbl = new QLabel(
                QStringLiteral("<span style='color:#8b949e;font-size:11px'>%1</span>")
                    .arg(signal.detail.toHtmlEscaped()));
            detailLbl->setTextFormat(Qt::RichText);
            vlay->addWidget(detailLbl);
        }
        return card;
    }

    // Count outdated (loose-specifier) alerts per manifest from findings
    QHash<QString, int> outdatedByPath;
    for (const RepoSecurityFinding &f : findings) {
        if (f.category == QLatin1String("Dependency"))
            outdatedByPath[f.path]++;
    }

    // Manifest table: header row + one row per manifest
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 8, 0, 0);
    grid->setSpacing(4);
    grid->setColumnStretch(0, 1);

    auto makeHdr = [](const QString &text) {
        auto *lbl = new QLabel(
            QStringLiteral("<span style='color:#8b949e;font-size:11px;font-weight:600'>"
                           "%1</span>")
                .arg(text.toHtmlEscaped()));
        lbl->setTextFormat(Qt::RichText);
        return lbl;
    };
    grid->addWidget(makeHdr(QStringLiteral("Manifest")), 0, 0);
    grid->addWidget(makeHdr(QStringLiteral("Dependencies")), 0, 1, Qt::AlignRight);
    grid->addWidget(makeHdr(QStringLiteral("Outdated")), 0, 2, Qt::AlignRight);
    grid->addWidget(makeHdr(QStringLiteral("Vulnerable")), 0, 3, Qt::AlignRight);
    // column 4 reserved for buttons (no header needed)

    int row = 1;
    for (const QString &manifest : signal.items) {
        const QString href = QString::fromUtf8(QUrl::toPercentEncoding(manifest));
        auto *pathLbl = new QLabel(
            QStringLiteral("<a style='color:#58a6ff;text-decoration:none' "
                           "href='manifest:%1'>%2</a>")
                .arg(href, manifest.toHtmlEscaped()));
        pathLbl->setTextFormat(Qt::RichText);
        pathLbl->setTextInteractionFlags(Qt::TextBrowserInteraction);
        if (!localBase.trimmed().isEmpty()) {
            const QString abs = QDir(localBase).filePath(manifest);
            QObject::connect(pathLbl, &QLabel::linkActivated, context,
                             [abs](const QString &) {
                                 if (QFileInfo::exists(abs))
                                     QDesktopServices::openUrl(
                                         QUrl::fromLocalFile(abs));
                             });
        }

        const int depCount = dependencyCounts.value(manifest, 0);
        const QString depHtml =
            QStringLiteral("<span style='color:#c9d1d9;font-size:12px'>%1</span>")
                .arg(depCount);
        auto *depLbl = new QLabel(depHtml);
        depLbl->setTextFormat(Qt::RichText);
        depLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        const int outdated = outdatedByPath.value(manifest, 0);
        const QString outdatedHtml =
            outdated > 0
                ? QStringLiteral("<span style='color:#d29922;font-size:12px'>%1</span>")
                      .arg(outdated)
                : QStringLiteral("<span style='color:#3fb950;font-size:12px'>0</span>");
        auto *outdatedLbl = new QLabel(outdatedHtml);
        outdatedLbl->setTextFormat(Qt::RichText);
        outdatedLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // Vulnerable count: not yet implemented (requires external advisory DB)
        auto *vulnLbl = new QLabel(
            QStringLiteral("<span style='color:#8b949e;font-size:12px'>&ndash;</span>"));
        vulnLbl->setTextFormat(Qt::RichText);
        vulnLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        const bool isScanning = scanning && (scanningPath == manifest);
        auto *scanBtn = new QPushButton(isScanning ? QStringLiteral("Scanning\xE2\x80\xA6")
                                                   : QStringLiteral("Run scan"));
        scanBtn->setObjectName("ghostButton");
        scanBtn->setProperty("buttonSize", "sm");
        scanBtn->setEnabled(!scanning);
        scanBtn->setCursor(scanning ? Qt::BusyCursor : Qt::PointingHandCursor);
        QObject::connect(scanBtn, &QPushButton::clicked, context,
                         [runScan, manifest]() { runScan(manifest); });

        grid->addWidget(pathLbl, row, 0);
        grid->addWidget(depLbl, row, 1, Qt::AlignRight);
        grid->addWidget(outdatedLbl, row, 2, Qt::AlignRight);
        grid->addWidget(vulnLbl, row, 3, Qt::AlignRight);
        grid->addWidget(scanBtn, row, 4, Qt::AlignRight);
        ++row;
    }

    vlay->addLayout(grid);
    return card;
}

RepoSecurityInput MainWindow::buildRepoSecurityInput(const RepositoryRecord &selected,
                                                     const RepositoryRecord &writable) const
{
    RepoSecurityInput input;
    input.owner = selected.owner;
    input.name = selected.name;
    input.localPath = writable.localPath;
    input.mirrorPath = writable.mirrorPath.isEmpty() ? selected.mirrorPath
                                                     : writable.mirrorPath;
    input.publishToNetwork = selected.publishToNetwork;
    input.isPrivate = selected.isPrivate;
    input.previewOnly = selected.previewOnly;
    input.actionsEnabled = selected.actionsEnabled;
    input.integrityWarning = m_pinWarningActive;
    input.issues =
        IssueStore(writable.localPath, input.mirrorPath, &m_profileIdentity, m_userName)
            .loadAll();
    input.workflows = availableWorkflowsForRepo(writable);
    for (const ActionRun &run : std::as_const(m_actionRuns))
        if (run.owner == selected.owner && run.name == selected.name)
            input.actionRuns.append(run);
    return input;
}

void MainWindow::applyRepoSecuritySnapshot(const RepoSecuritySnapshot &snapshot,
                                           const QString &localBase)
{
    m_lastRepoSecuritySnapshot = snapshot;

    while (QLayoutItem *item = m_securitySignalsGrid->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    TableRepaintGuard repaintGuard(m_securityFindingsTable);
    m_securityFindingsTable->setSortingEnabled(false);
    m_securityFindingsTable->setRowCount(0);

    const RepoSecuritySeverity highest = RepoSecurity::highestSeverity(snapshot);
    int warnings = 0;
    int severe = 0;
    for (const RepoSecuritySignal &signal : snapshot.signalList) {
        if (signal.severity == RepoSecuritySeverity::Warning)
            ++warnings;
        else if (signal.severity == RepoSecuritySeverity::High ||
                 signal.severity == RepoSecuritySeverity::Critical)
            ++severe;
    }
    for (const RepoSecurityFinding &finding : snapshot.findings) {
        if (finding.severity == RepoSecuritySeverity::Warning)
            ++warnings;
        else if (finding.severity == RepoSecuritySeverity::High ||
                 finding.severity == RepoSecuritySeverity::Critical)
            ++severe;
    }

    m_securitySummary->setText(
        QStringLiteral(
            "<div style='font-size:21px; font-weight:800; color:%1'>%2</div>"
            "<div style='color:#8b949e; font-size:12px; font-weight:600'>"
            "%3 at %4. %5 finding%6, %7 warning%8, %9 severe.</div>")
            .arg(repoSecuritySeverityColor(highest),
                 RepoSecurity::severityText(highest).toHtmlEscaped(),
                 snapshot.repoKey.toHtmlEscaped(), snapshot.ref.toHtmlEscaped())
            .arg(snapshot.findings.size())
            .arg(snapshot.findings.size() == 1 ? QString() : QStringLiteral("s"))
            .arg(warnings)
            .arg(warnings == 1 ? QString() : QStringLiteral("s"))
            .arg(severe));

    int index = 0;
    const RepoSecuritySignal *depSignal = nullptr;
    for (const RepoSecuritySignal &signal : snapshot.signalList) {
        // Dependency scan gets a dedicated full-width table card (added below)
        if (signal.key == QLatin1String("dependencies")) {
            depSignal = &signal;
            continue;
        }
        auto *card = new QLabel(repoSecuritySignalHtml(signal));
        card->setObjectName("insightsCard");
        card->setTextFormat(Qt::RichText);
        card->setWordWrap(true);
        card->setMinimumHeight(92);
        const int row = index / 3;
        const int col = index % 3;
        m_securitySignalsGrid->addWidget(card, row, col);
        ++index;
    }
    // Place the dependency-scan card in a dedicated full-width row
    if (depSignal) {
        const int depRow = (index + 2) / 3;
        auto *depCard = buildDepScanCard(
            *depSignal, snapshot.findings, snapshot.dependencyCounts, snapshot.generatedAtMs,
            localBase, this, m_repoSecurityScanRunning, m_repoSecurityScanningPath,
            [this](const QString &manifestPath) { runRepoDependencyScan(manifestPath); });
        m_securitySignalsGrid->addWidget(depCard, depRow, 0, 1, 4);
    }

    fillRepoFindingsTable(m_securityFindingsTable, snapshot.findings,
                          QStringLiteral("No local findings at this ref."));

    m_securityFindingsTable->setSortingEnabled(true);
}

void MainWindow::refreshRepoSecurity()
{
    if (!m_securitySummary || !m_securitySignalsGrid || !m_securityFindingsTable)
        return;

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        while (QLayoutItem *item = m_securitySignalsGrid->takeAt(0)) {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        TableRepaintGuard repaintGuard(m_securityFindingsTable);
        m_securityFindingsTable->setSortingEnabled(false);
        m_securityFindingsTable->setRowCount(0);
        m_securitySummary->setText(
            "<b>Security</b><br><span style='color:#8b949e'>"
            "Select a repository to scan local evidence.</span>");
        m_securityFindingsTable->setSortingEnabled(true);
        return;
    }

    const RepositoryRecord &selected = m_repositories.at(m_repoDetailIndex);
    const RepositoryRecord &writable = writableRecordFor(selected);
    m_repoSecurityScanRunning = false;
    m_repoSecurityScanningPath.clear();
    applyRepoSecuritySnapshot(
        RepoSecurity::scan(buildRepoSecurityInput(selected, writable)),
        writable.localPath);
}

void MainWindow::runRepoDependencyScan(const QString &manifestPath)
{
    if (m_repoSecurityScanRunning)
        return;
    if (!m_securitySummary || !m_securitySignalsGrid || !m_securityFindingsTable)
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;

    const RepositoryRecord &selected = m_repositories.at(m_repoDetailIndex);
    const RepositoryRecord &writable = writableRecordFor(selected);
    const RepoSecurityInput input = buildRepoSecurityInput(selected, writable);
    const QString startOwner = selected.owner;
    const QString startName = selected.name;

    m_repoSecurityScanRunning = true;
    m_repoSecurityScanningPath = manifestPath;
    // Re-render immediately using the last-known snapshot so the button/progress
    // bar flip to their busy state right away, before the rescan itself (which
    // runs off-thread below) has produced anything new.
    applyRepoSecuritySnapshot(m_lastRepoSecuritySnapshot, writable.localPath);

    if (manifestPath.isEmpty()) {
        // Full repo scan
        runOffThread<RepoSecuritySnapshot>(
            [input]() { return RepoSecurity::scan(input); },
            [this, startOwner, startName](RepoSecuritySnapshot snapshot) {
                m_repoSecurityScanRunning = false;
                m_repoSecurityScanningPath.clear();
                if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
                    return;
                const RepositoryRecord &current = m_repositories.at(m_repoDetailIndex);
                if (current.owner != startOwner || current.name != startName)
                    return; // user navigated to a different repo while the scan ran
                applyRepoSecuritySnapshot(snapshot, writableRecordFor(current).localPath);
            });
    } else {
        // Single-manifest scan: update just that manifest's findings and dependency count
        runOffThread<RepoSecurityManifestScan>(
            [input, manifestPath]() { return RepoSecurity::scanManifest(input, manifestPath); },
            [this, startOwner, startName, manifestPath](RepoSecurityManifestScan result) {
                m_repoSecurityScanRunning = false;
                m_repoSecurityScanningPath.clear();
                if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
                    return;
                const RepositoryRecord &current = m_repositories.at(m_repoDetailIndex);
                if (current.owner != startOwner || current.name != startName)
                    return;

                // Update the snapshot with new findings for this manifest
                RepoSecuritySnapshot &snapshot = m_lastRepoSecuritySnapshot;
                snapshot.dependencyCounts[manifestPath] = result.dependencyCount;
                // Replace old findings for this manifest with new ones
                snapshot.findings.erase(
                    std::remove_if(snapshot.findings.begin(), snapshot.findings.end(),
                                   [&manifestPath](const RepoSecurityFinding &f) {
                                       return f.path == manifestPath && f.category == QLatin1String("Dependency");
                                   }),
                    snapshot.findings.end());
                snapshot.findings.append(result.findings);

                applyRepoSecuritySnapshot(snapshot, writableRecordFor(current).localPath);
            });
    }
}

void MainWindow::openRepoFileAtLine(const QString &path, int line)
{
    openRepoFile(path);
    if (line <= 0)
        return;
    QTimer::singleShot(0, this, [this, path, line] {
        auto *edit = qobject_cast<QPlainTextEdit *>(m_openFileTabs.value(path));
        if (!edit)
            return;
        QTextCursor lc(edit->document());
        lc.movePosition(QTextCursor::Start);
        if (line > 1)
            lc.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line - 1);
        QTextEdit::ExtraSelection lineSel;
        lineSel.cursor = lc;
        lineSel.format.setBackground(QColor(31, 111, 235, 60));
        lineSel.format.setProperty(QTextFormat::FullWidthSelection, true);
        // Move the cursor (and focus) first: CodePreviewEditor's own
        // cursorPositionChanged handler calls highlightCurrentLine(), which
        // replaces the extra-selection list with its neutral current-line
        // background. Applying our highlight after that keeps the distinct
        // blue finding highlight instead of it being silently overwritten.
        edit->setTextCursor(lc);
        edit->setFocus();
        edit->centerCursor();
        edit->setExtraSelections({lineSel});
    });
}

QWidget *MainWindow::buildRepoQualityTab()
{
    auto *page = new QWidget;
    page->setObjectName("mainContent");

    auto *scroll = new QScrollArea;
    scroll->setObjectName("mainContent");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    content->setObjectName("insightsPage");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);

    auto *heading = new QLabel("Quality");
    heading->setObjectName("qualityTitle");
    heading->setProperty("class", "channelTitle");
    heading->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;"));
    auto *subtitle = new QLabel(
        "Local quality metrics for this repository: check runs, code volume, "
        "documentation, tests, maintenance markers, file sizes, commit "
        "activity and issue hygiene.");
    subtitle->setObjectName("statusLine");
    subtitle->setWordWrap(true);

    m_qualityRefreshButton = new QPushButton("Refresh");
    m_qualityRefreshButton->setObjectName("ghostButton");
    m_qualityRefreshButton->setProperty("buttonSize", "sm");
    m_qualityRefreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_qualityRefreshButton, "sync", 16);
    connect(m_qualityRefreshButton, &QPushButton::clicked, this,
            &MainWindow::refreshRepoQuality);

    auto *headingCol = new QVBoxLayout;
    headingCol->setContentsMargins(0, 0, 0, 0);
    headingCol->setSpacing(3);
    headingCol->addWidget(heading);
    headingCol->addWidget(subtitle);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);
    headerRow->addLayout(headingCol, 1);
    headerRow->addWidget(m_qualityRefreshButton, 0, Qt::AlignTop);
    layout->addLayout(headerRow);

    m_qualitySummary = new QLabel;
    m_qualitySummary->setObjectName("insightsCard");
    m_qualitySummary->setTextFormat(Qt::RichText);
    m_qualitySummary->setWordWrap(true);
    m_qualitySummary->setMinimumHeight(92);
    layout->addWidget(m_qualitySummary);

    auto *metricsLabel = new QLabel("METRICS");
    metricsLabel->setObjectName("sectionLabel");
    layout->addWidget(metricsLabel);

    m_qualitySignalsPanel = new QWidget;
    m_qualitySignalsGrid = new QGridLayout(m_qualitySignalsPanel);
    m_qualitySignalsGrid->setContentsMargins(0, 0, 0, 0);
    m_qualitySignalsGrid->setSpacing(10);
    layout->addWidget(m_qualitySignalsPanel);

    auto *findingsLabel = new QLabel("FINDINGS");
    findingsLabel->setObjectName("sectionLabel");
    layout->addWidget(findingsLabel);

    m_qualityFindingsTable = new QTableWidget(0, 4);
    installColumnHeaderMenu(m_qualityFindingsTable); // 3-dots per-column menu (issue #318)
    m_qualityFindingsTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_qualityFindingsTable);
    m_qualityFindingsTable->setHorizontalHeaderLabels(
        {"Severity", "Category", "Finding", "Suggestion"});
    m_qualityFindingsTable->verticalHeader()->setVisible(false);
    m_qualityFindingsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_qualityFindingsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_qualityFindingsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_qualityFindingsTable->setShowGrid(false);
    m_qualityFindingsTable->setWordWrap(false);
    QHeaderView *header = m_qualityFindingsTable->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_qualityFindingsTable);
    m_qualityFindingsTable->setMinimumHeight(220);
    layout->addWidget(m_qualityFindingsTable);
    layout->addStretch();
    connect(m_qualityFindingsTable, &QTableWidget::cellClicked,
            this, [this](int row, int /*col*/) {
        auto *item = m_qualityFindingsTable->item(row, 2);
        if (!item)
            return;
        const QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty())
            return;
        // Switch to the Code tab (index 0) so the highlighted line is visible;
        // openRepoFileAtLine alone only touches the (currently hidden) files panel.
        if (m_repoDetailTabs && m_repoDetailTabs->button(0))
            m_repoDetailTabs->button(0)->click();
        openRepoFileAtLine(path, item->data(Qt::UserRole + 1).toInt());
    });

    scroll->setWidget(content);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scroll);
    refreshRepoQuality();
    return page;
}

void MainWindow::refreshRepoQuality()
{
    if (!m_qualitySummary || !m_qualitySignalsGrid || !m_qualityFindingsTable)
        return;

    while (QLayoutItem *item = m_qualitySignalsGrid->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    TableRepaintGuard repaintGuard(m_qualityFindingsTable);
    m_qualityFindingsTable->setSortingEnabled(false);
    m_qualityFindingsTable->setRowCount(0);

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        m_qualitySummary->setText(
            "<b>Quality</b><br><span style='color:#8b949e'>"
            "Select a repository to compute quality metrics.</span>");
        m_qualityFindingsTable->setSortingEnabled(true);
        return;
    }

    const RepositoryRecord &selected = m_repositories.at(m_repoDetailIndex);
    const RepositoryRecord &writable = writableRecordFor(selected);

    RepoSecurityInput input;
    input.owner = selected.owner;
    input.name = selected.name;
    input.localPath = writable.localPath;
    input.mirrorPath = writable.mirrorPath.isEmpty() ? selected.mirrorPath
                                                     : writable.mirrorPath;
    input.publishToNetwork = selected.publishToNetwork;
    input.isPrivate = selected.isPrivate;
    input.previewOnly = selected.previewOnly;
    input.actionsEnabled = selected.actionsEnabled;
    input.issues =
        IssueStore(writable.localPath, input.mirrorPath, &m_profileIdentity, m_userName)
            .loadAll();
    input.workflows = availableWorkflowsForRepo(writable);
    for (const ActionRun &run : std::as_const(m_actionRuns))
        if (run.owner == selected.owner && run.name == selected.name)
            input.actionRuns.append(run);

    const RepoSecuritySnapshot snapshot = RepoQuality::scan(input);
    const RepoSecuritySeverity highest = RepoSecurity::highestSeverity(snapshot);
    int warnings = 0;
    int severe = 0;
    for (const RepoSecuritySignal &signal : snapshot.signalList) {
        if (signal.severity == RepoSecuritySeverity::Warning)
            ++warnings;
        else if (signal.severity == RepoSecuritySeverity::High ||
                 signal.severity == RepoSecuritySeverity::Critical)
            ++severe;
    }
    for (const RepoSecurityFinding &finding : snapshot.findings) {
        if (finding.severity == RepoSecuritySeverity::Warning)
            ++warnings;
        else if (finding.severity == RepoSecuritySeverity::High ||
                 finding.severity == RepoSecuritySeverity::Critical)
            ++severe;
    }

    m_qualitySummary->setText(
        QStringLiteral(
            "<div style='font-size:21px; font-weight:800; color:%1'>%2</div>"
            "<div style='color:#8b949e; font-size:12px; font-weight:600'>"
            "%3 at %4. %5 finding%6, %7 warning%8, %9 severe.</div>")
            .arg(repoSecuritySeverityColor(highest),
                 RepoSecurity::severityText(highest).toHtmlEscaped(),
                 snapshot.repoKey.toHtmlEscaped(), snapshot.ref.toHtmlEscaped())
            .arg(snapshot.findings.size())
            .arg(snapshot.findings.size() == 1 ? QString() : QStringLiteral("s"))
            .arg(warnings)
            .arg(warnings == 1 ? QString() : QStringLiteral("s"))
            .arg(severe));

    int index = 0;
    for (const RepoSecuritySignal &signal : snapshot.signalList) {
        auto *card = new QLabel(repoSecuritySignalHtml(signal));
        card->setObjectName("insightsCard");
        card->setTextFormat(Qt::RichText);
        card->setWordWrap(true);
        card->setMinimumHeight(92);
        const int row = index / 3;
        const int col = index % 3;
        m_qualitySignalsGrid->addWidget(card, row, col);
        ++index;
    }

    fillRepoFindingsTable(m_qualityFindingsTable, snapshot.findings,
                          QStringLiteral("No quality findings at this ref."));

    m_qualityFindingsTable->setSortingEnabled(true);
}

QWidget *MainWindow::buildInsightsTab()
{
    auto *page = new QWidget;
    page->setObjectName("mainContent");

    auto *scroll = new QScrollArea;
    scroll->setObjectName("mainContent");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    content->setObjectName("insightsPage");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);

    auto *heading = new QLabel("Insights");
    heading->setObjectName("channelTitle");
    auto *subtitle = new QLabel(
        "Local repository and ForkMesh traffic metrics. No data leaves this node.");
    subtitle->setObjectName("statusLine");
    subtitle->setWordWrap(true);

    m_insightsRefreshButton = new QPushButton("Refresh");
    m_insightsRefreshButton->setObjectName("ghostButton");
    m_insightsRefreshButton->setProperty("buttonSize", "sm");
    m_insightsRefreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_insightsRefreshButton, "sync", 16);
    connect(m_insightsRefreshButton, &QPushButton::clicked, this,
            &MainWindow::loadRepoInsights);
    addRefreshSpin(m_insightsRefreshButton);

    auto *headingCol = new QVBoxLayout;
    headingCol->setContentsMargins(0, 0, 0, 0);
    headingCol->setSpacing(3);
    headingCol->addWidget(heading);
    headingCol->addWidget(subtitle);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);
    headerRow->addLayout(headingCol, 1);
    headerRow->addWidget(m_insightsRefreshButton, 0, Qt::AlignTop);
    layout->addLayout(headerRow);

    m_insightsSummary = new QLabel;
    m_insightsSummary->setObjectName("insightsCard");
    m_insightsSummary->setTextFormat(Qt::RichText);
    m_insightsSummary->setWordWrap(true);
    m_insightsSummary->setMinimumHeight(110);

    m_insightsTraffic = new QLabel;
    m_insightsTraffic->setObjectName("insightsCard");
    m_insightsTraffic->setTextFormat(Qt::RichText);
    m_insightsTraffic->setWordWrap(true);
    m_insightsTraffic->setMinimumHeight(110);

    auto *summaryRow = new QHBoxLayout;
    summaryRow->setContentsMargins(0, 0, 0, 0);
    summaryRow->setSpacing(10);
    summaryRow->addWidget(m_insightsSummary, 2);
    summaryRow->addWidget(m_insightsTraffic, 1);
    layout->addLayout(summaryRow);

    auto *languageLabel = new QLabel("LANGUAGES");
    languageLabel->setObjectName("sectionLabel");
    m_insightsLanguageBar = new QLabel;
    m_insightsLanguageBar->setObjectName("langBar");
    m_insightsLanguageBar->setFixedHeight(8);
    m_insightsLanguageBar->setTextFormat(Qt::RichText);
    m_insightsLanguageLegend = new QLabel;
    m_insightsLanguageLegend->setObjectName("statusLine");
    m_insightsLanguageLegend->setTextFormat(Qt::RichText);
    m_insightsLanguageLegend->setWordWrap(true);
    layout->addWidget(languageLabel);
    layout->addWidget(m_insightsLanguageBar);
    layout->addWidget(m_insightsLanguageLegend);

    auto configureTable = [](QTableWidget *table) {
        table->setObjectName("issueTable");
        enableHoverRowHighlight(table);
        table->verticalHeader()->setVisible(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setShowGrid(false);
        table->setWordWrap(false);
        table->setSortingEnabled(false);
        table->horizontalHeader()->setHighlightSections(false);
    };

    // Merged "Contributors & activity": one row per contributor carrying the
    // commit count, share, and a commits-over-time bar chart (formerly two
    // separate sections). A time-range selector rescopes every column.
    auto *contributorsLabel = new QLabel("CONTRIBUTORS & ACTIVITY");
    contributorsLabel->setObjectName("sectionLabel");

    m_insightsRangeCombo = new QComboBox;
    m_insightsRangeCombo->setObjectName("ghostCombo");
    m_insightsRangeCombo->setCursor(Qt::PointingHandCursor);
    // userData = window in days; 0 means all time.
    m_insightsRangeCombo->addItem("All time", 0);
    m_insightsRangeCombo->addItem("Last 12 months", 365);
    m_insightsRangeCombo->addItem("Last 90 days", 90);
    m_insightsRangeCombo->addItem("Last 30 days", 30);
    m_insightsRangeCombo->addItem("Last 7 days", 7);
    connect(m_insightsRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this] { loadRepoInsights(); });

    auto *contributorsHeader = new QHBoxLayout;
    contributorsHeader->setContentsMargins(0, 0, 0, 0);
    contributorsHeader->setSpacing(8);
    contributorsHeader->addWidget(contributorsLabel, 1, Qt::AlignBottom);
    contributorsHeader->addWidget(new QLabel("Activity range:"), 0, Qt::AlignVCenter);
    contributorsHeader->addWidget(m_insightsRangeCombo, 0, Qt::AlignVCenter);

    m_insightsContributors = new QTableWidget(0, 4);
    installColumnHeaderMenu(m_insightsContributors); // 3-dots per-column menu (issue #318)
    m_insightsContributors->setHorizontalHeaderLabels(
        {"Contributor", "Commits", "Share", "Activity"});
    configureTable(m_insightsContributors);
    m_insightsContributors->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_insightsContributors->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    m_insightsContributors->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    m_insightsContributors->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::Stretch);
    makeColumnsResizable(m_insightsContributors);
    m_insightsContributors->verticalHeader()->setDefaultSectionSize(34);
    m_insightsContributors->setMinimumHeight(260);
    // Right-click a contributor to reassign their commits to another identity.
    m_insightsContributors->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_insightsContributors, &QWidget::customContextMenuRequested, this,
            &MainWindow::showInsightsContributorMenu);
    // Left-click a contributor's name or commit count to open the Commits tab
    // filtered to that author.
    connect(m_insightsContributors, &QTableWidget::cellClicked, this,
            [this](int row, int column) {
                if (column != 0 && column != 1)
                    return;
                QTableWidgetItem *nameItem = m_insightsContributors->item(row, 0);
                if (!nameItem)
                    return;
                const QString name = nameItem->data(Qt::UserRole).toString().isEmpty()
                                         ? nameItem->text()
                                         : nameItem->data(Qt::UserRole).toString();
                openCommitsForContributor(name);
            });

    // Caption under the table: the span the activity bars cover, oldest to newest.
    m_insightsActivityAxis = new QLabel;
    m_insightsActivityAxis->setObjectName("statusLine");
    m_insightsActivityAxis->setTextFormat(Qt::RichText);
    m_insightsActivityAxis->setWordWrap(true);

    layout->addLayout(contributorsHeader);
    layout->addWidget(m_insightsContributors);
    layout->addWidget(m_insightsActivityAxis);

    m_insightsActivity = new QLabel;
    m_insightsActivity->setObjectName("statusLine");
    m_insightsActivity->setTextFormat(Qt::RichText);
    m_insightsActivity->setWordWrap(true);
    layout->addWidget(m_insightsActivity);
    layout->addStretch();

    scroll->setWidget(content);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scroll);
    return page;
}

QWidget *MainWindow::buildPlaceholderTab(const QString &name)
{
    auto *page = new QWidget;
    auto *label = new QLabel(
        QStringLiteral("<b>%1</b><br><span style='color:#8b949e'>Not available in "
                       "ForkMesh yet.</span>")
            .arg(name));
    label->setObjectName("placeholderPanel");
    label->setAlignment(Qt::AlignCenter);
    label->setTextFormat(Qt::RichText);
    auto *layout = new QVBoxLayout(page);
    layout->addStretch();
    layout->addWidget(label, 0, Qt::AlignCenter);
    layout->addStretch();
    return page;
}

