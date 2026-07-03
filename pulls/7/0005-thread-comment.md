---
type: thread-comment
id: d2047c2f-5f4c-4033-aae7-45389b72a616
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
ts: 1783083381597
threadId: d2047c2f-5f4c-4033-aae7-45389b72a616
path: flutter_app/lib/screens/home_shell.dart
side: new
lineStart: 57
lineEnd: 58
sig: uZx0EqpI-GhirtwFuk3fP-Ch15dsbeVFm9jRiOGqwCEoypE_MydLWCamhMiQaGiSfT3xiVjpnCyLCWcXAVH1AQ
---

**🤖 AI review · bug:** The destinations list now has 5 entries (Repos, Chat, Activity, Profile, Tools) but the pages list only has 4. Since Tools opens a bottom sheet rather than switching pages, the index mapping is fine for Tools, but Profile is destination index 3 and maps to pages[3] = SettingsScreen which is correct; however the labels list adds Activity/Profile/Tools while pages order is Repos,Chat,Activity,Settings — the 'Profile' label at index 3 maps to SettingsScreen, a mismatch between label and screen.
