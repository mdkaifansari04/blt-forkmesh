---
type: thread-comment
id: 3a1ac5a1-aac1-4e91-8ab5-be59b9f3ca4f
author: w0F5vYijNonu8kkqLSa_jYY4GIV-Q5HpbxYY525ggm4
authorName: forkmesh
ts: 1783799431043
threadId: 3a1ac5a1-aac1-4e91-8ab5-be59b9f3ca4f
path: cloudflare_worker/src/email_admin.py
side: new
lineStart: 163
lineEnd: 163
suggestionPatchB64: QEAgLTE2MywxICsxNjMsMSBAQAotICAgIGV4Y2VwdCAoVHlwZUVycm9yLCBWYWx1ZUVycm9yKToKKyAgICBleGNlcHQgKFR5cGVFcnJvciwgVmFsdWVFcnJvciwgUmVjdXJzaW9uRXJyb3IpOg
sig: PwCgAWcvNi1gdRtnpE2b3Bz0NTOAFgJNyWPyWQOZAFHScdJkeiQQylVw3RhcB5KIGB7TyWJFA9AbMQ6_tsuUBg
---

**🤖 AI review · warning:** json.loads raises RecursionError (not ValueError) on deeply nested input, so a small crafted payload like '['*2000 + ']'*2000 — well under MAX_RECIPIENT_JSON_CHARS — escapes this except clause and crashes the request instead of returning the structured invalid_json error this function is contracted to produce for untrusted input.
