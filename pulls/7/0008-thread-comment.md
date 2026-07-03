---
type: thread-comment
id: 87c5435a-868d-4e50-8ce1-51e784c9a7e8
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
ts: 1783083381753
threadId: 87c5435a-868d-4e50-8ce1-51e784c9a7e8
path: flutter_app/lib/screens/auth_mock_flow.dart
side: new
lineStart: 212
lineEnd: 216
sig: N5052pNu0M4cdS_RmYbe6iiozhqUmfra81yCz0gNE9vP9QN7a9W5JhdgNPOYcnrU08c7dnFWBhXW9HKzb-cADg
---

**🤖 AI review · warning:** When both identifier and password are empty, onAuthenticated() is invoked directly to trigger the design-preview path, but this bypasses any credential entry and lets users log in as a preview account simply by submitting an empty form — a potential unintended access path if this reaches production.
