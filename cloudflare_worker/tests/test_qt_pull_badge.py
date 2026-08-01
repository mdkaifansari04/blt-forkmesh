#!/usr/bin/env python3
"""Qt desktop Badge tab for pull requests (adhoc #44).

The PR detail sub-tab bar gains a fifth tab, "Badge", backed by
PullBadgeWidget: one bundled file-type icon tile per changed file with a
green/red additions:deletions ratio bar, clustered per directory under a
labeled connector line, plus a header with title/number/author/totals.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
QT_SRC = ROOT / "qt_client" / "src"
PULLS = (QT_SRC / "MainWindowPulls.cpp").read_text(encoding="utf-8")
WIDGET_H = (QT_SRC / "PullBadgeWidget.h").read_text(encoding="utf-8")
WIDGET_CPP = (QT_SRC / "PullBadgeWidget.cpp").read_text(encoding="utf-8")


def test_badge_subtab_registered_after_files_changed():
    tabs = PULLS[PULLS.index("const QList<QPair<QString, const char *>> subTabs"):]
    tabs = tabs[:tabs.index(";")]
    assert '{QStringLiteral("Badge"), "graph"}' in tabs

    assert "m_pullSubStack->addWidget(badgeScroll);        // 4 Badge" in PULLS
    assert ("m_pullTabBadge = qobject_cast<QPushButton *>"
            "(m_pullSubTabs->button(4));") in PULLS


def test_badge_widget_scrolls_and_is_fed_per_file_stats():

    start = PULLS.index("m_pullBadgeWidget = new PullBadgeWidget;")
    body = PULLS[start:start + 400]
    assert "badgeScroll->setWidgetResizable(true);" in body
    assert "badgeScroll->setWidget(m_pullBadgeWidget);" in body


    feed = PULLS[PULLS.index("QList<PullBadgeWidget::FileEntry> badgeFiles;"):]
    feed = feed[:feed.index("m_pullBadgeWidget->setPull(") + 400]
    assert 'line.startsWith(QLatin1String("+++"))' in feed
    assert 'line.startsWith(QLatin1String("---"))' in feed
    assert "iconForFile(it.key().section('/', -1))" in feed

    assert "m_pullBadgeWidget->clearPull();" in PULLS


def test_badge_widget_paints_ratio_bar_and_directory_labels():

    assert "double(file.adds) / total" in WIDGET_CPP


    assert 'QStringLiteral("%1  (%2 file%3)")' in WIDGET_CPP
    assert ".arg(dirBadgeLabel(dir))" in WIDGET_CPP
    assert "name.left(8)" in WIDGET_CPP

    assert "std::sort(m_files.begin(), m_files.end()" in WIDGET_CPP

    assert "QEvent::ToolTip" in WIDGET_CPP
    assert "struct FileEntry" in WIDGET_H
