---
schema: forkmesh-issue-v1
number: 190
title: warning when restarting
status: closed
labels: []
milestone: 
priority: 0
progress: 100
assignees: []
createdAt: 1782257145073
author: DBSTPqC2ki3F3INajdL6-mCKg7Jk58uuzrsIA4RGYtI
authorName: nodedbstpqc2
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-190
ts: 1782257145073
attachments: []
sig: xKb-lOX9iio04eoAJccBpvTCJmxIGBw5gnfE5Xo2GFdL10HhTDQXf28MbO0gfiQO1NqYG3KhHwxK4xQnHhROCA
---

/home/user/.local/share/ForkMesh/ForkMesh/src/qt_client/src/TerminalWidget.cpp: In member function ‘void TerminalWidget::runCommand(const QString&, const QString&, const QStringList&)’:
/home/user/.local/share/ForkMesh/ForkMesh/src/qt_client/src/TerminalWidget.cpp:203:26: warning: ignoring return value of ‘int chdir(const char*)’ declared with attribute ‘warn_unused_result’ [-Wunused-result]
  203 |             (void)::chdir(cwdBytes.constData());
      |                   ~~~~~~~^~~~~~~~~~~~~~~~~~~~~~
