---
type: thread-comment
id: 2307859c-92c9-4d1f-ad2a-3df7067fd71f
author: w0F5vYijNonu8kkqLSa_jYY4GIV-Q5HpbxYY525ggm4
authorName: forkmesh
ts: 1783799431078
threadId: 2307859c-92c9-4d1f-ad2a-3df7067fd71f
path: cloudflare_worker/src/email_admin.py
side: new
lineStart: 261
lineEnd: 261
suggestionPatchB64: QEAgLTI2MSwxICsyNjEsNiBAQAotICAgIHJlbmRlcmVkX3RleHQgPSBfcmVwbGFjZV9wbGFjZWhvbGRlcnMoYm9keSwgcmVjaXBpZW50KS5zdHJpcCgpCisgICAgcmVuZGVyZWRfdGV4dCA9IF9yZXBsYWNlX3BsYWNlaG9sZGVycyhib2R5LCByZWNpcGllbnQpLnN0cmlwKCkKKyAgICBib2R5X2xpbmVfY29udGVudCA9ICgKKyAgICAgICAgcmVuZGVyZWRfdGV4dC5yZXBsYWNlKCJcciIsICIiKS5yZXBsYWNlKCJcbiIsICIiKS5yZXBsYWNlKCJcdCIsICIiKQorICAgICkKKyAgICBpZiBfaGFzX3Vuc2FmZV90ZXh0X2NoYXJhY3Rlcihib2R5X2xpbmVfY29udGVudCk6CisgICAgICAgIHJhaXNlIFZhbHVlRXJyb3IoImJvZHkgY29udGFpbnMgdW5zYWZlIGNoYXJhY3RlcnMiKQ
sig: x2QXyvS2Bn2TVHwsqeIhncveupJb9UFGelMyS4UgbNcmkiBpzj96qiVJPaW3npnafvheyLdqW8zGxGM7A1enCQ
---

**🤖 AI review · warning:** _render_field deliberately returns unsafe values raw (untruncated) so the subject check can reject them, but the body path has no equivalent check: a recipient name/company containing control characters (NUL, \x1b, \u2028, etc.) is embedded verbatim into the rendered text/html body, and such values also bypass the 120/160-char truncation since _render_field skips [:limit] for them. Only the subject is validated post-render.
