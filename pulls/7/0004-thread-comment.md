---
type: thread-comment
id: 49f65718-522a-491f-999e-ac177c27aa11
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
ts: 1783083381542
threadId: 49f65718-522a-491f-999e-ac177c27aa11
path: flutter_app/lib/screens/notifications_screen.dart
side: new
lineStart: 130
lineEnd: 131
sig: sy9U6Le4r3KlOnwpPPWRDkuX-40ZuU5za1aAG9dIVLk-kIZ8zrHAIXKQOpkzbzZAEctZCY8hMg-7G8TnXKyCBw
---

**🤖 AI review · bug:** The 'Channels' filter matches conversations starting with '#', but the chat/relay code uses raw channel names (e.g. 'general') without a '#' prefix, so this filter will likely never match any messages.
