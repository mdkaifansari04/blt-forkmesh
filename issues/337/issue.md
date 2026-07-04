---
schema: forkmesh-issue-v1
number: 337
title: Update qt app design
status: closed
labels: []
milestone: 
priority: 65
progress: 0
assignees: [Claude Code]
createdAt: 1783051676
author: HvXXnfD2USGnXue_hnzoVlyBZYdvJU7E5o_u3C8srtE
authorName: jett
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-web-1783051676
ts: 1783051676
attachments: []
sig: zcAZ2gcW81soW_dQ66jW7hI568GbI7lLNjUbFY3m-mgx7BhxSnWZIioreSQ27Kdlu_Fk7wjGbO3uk4DdolzBBw
---

A Qt app looks nicer when you improve spacing, typography, consistency, and visual hierarchy before adding flashy effects.

1. Use layouts properly

Avoid fixed pixel positions. Use QVBoxLayout, QHBoxLayout, QGridLayout, and set good margins/spacing:

auto *layout = new QVBoxLayout(this);
layout->setContentsMargins(16, 16, 16, 16);
layout->setSpacing(12);

Bad spacing makes Qt apps feel “default” quickly.

2. Pick a modern Qt style

Try Fusion as a baseline:

QApplication::setStyle("Fusion");

Then apply a light/dark palette or stylesheet.

3. Use a consistent font

Set one app-wide font:

QFont font("Inter", 10);
QApplication::setFont(font);

On Windows, Segoe UI is also a safe choice. On macOS, let the system font handle it unless you have a strong reason.

4. Add a clean stylesheet

For a quick modern look:

qApp->setStyleSheet(R"(
QWidget {
    font-size: 10pt;
}
QPushButton {
    padding: 8px 14px;
    border-radius: 6px;
    background-color: #2d7ff9;
    color: white;
    border: none;
}
QPushButton:hover {
    background-color: #1f6fe5;
}
QPushButton:pressed {
    background-color: #185abc;
}
QLineEdit, QTextEdit, QComboBox {
    padding: 6px 8px;
    border: 1px solid #c7c7c7;
    border-radius: 6px;
    background: white;
}
QGroupBox {
    font-weight: bold;
    margin-top: 12px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 8px;
    padding: 0 4px;
}
)");

5. Use icons

Icons make interfaces easier to scan. Add them to buttons, menus, tabs, and toolbars:

button->setIcon(QIcon(":/icons/save.svg"));

SVG icons are best because they scale cleanly.

6. Use fewer borders

A lot of default Qt UIs feel boxy. Prefer spacing, section titles, subtle backgrounds, and cards instead of heavy outlines everywhere.

Example card-like container:

QFrame *card = new QFrame;
card->setObjectName("Card");
card->setStyleSheet(R"(
QFrame#Card {
    background: #ffffff;
    border: 1px solid #e1e4e8;
    border-radius: 10px;
    padding: 12px;
}
)");

7. Improve empty states and messages

Instead of blank panels, show helpful text:

QLabel *empty = new QLabel("No projects yet. Create one to get started.");
empty->setAlignment(Qt::AlignCenter);
empty->setStyleSheet("color: #777; font-size: 11pt;");

8. Use QML for highly polished UIs

For desktop utility apps, Qt Widgets are fine. For more animated, modern, mobile-like interfaces, Qt Quick/QML is often easier to make beautiful.

9. Consider existing themes

For Python/PyQt/PySide, these are popular starting points:

pip install qdarktheme
import qdarktheme
app.setStyleSheet(qdarktheme.load_stylesheet())

For C++ Qt, you can use custom QSS themes, Fusion palettes, or libraries like Qt Material-style themes.

10. Focus on hierarchy

Make primary actions visually obvious, secondary actions quieter, and destructive actions distinct. A nice-looking app is usually less about colors and more about clarity.

A good quick path: use Fusion style + consistent margins + one font + SVG icons + a simple stylesheet.
