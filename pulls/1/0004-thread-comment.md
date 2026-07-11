---
type: thread-comment
id: fe99be81-a2f3-4ded-85d2-c26aa823dbb1
author: w0F5vYijNonu8kkqLSa_jYY4GIV-Q5HpbxYY525ggm4
authorName: forkmesh
ts: 1783799431111
threadId: fe99be81-a2f3-4ded-85d2-c26aa823dbb1
path: cloudflare_worker/migrations/0035_email_admin.sql
side: new
lineStart: 66
lineEnd: 66
sig: GcNWP-A1agVelVMQkbL4gfp9ciQmEFRX7jHTXXvGiO5nTq6gSM74BDyDhLv_9o5gUct8uHA3Ra_oU2aOU3LEAw
---

**🤖 AI review · nit:** The ON DELETE SET NULL foreign key on recipient_id has no supporting index (the UNIQUE(campaign_id, recipient_id) index cannot serve a recipient_id lookup), so every DELETE from email_recipients full-scans email_deliveries to enforce the FK — the sibling email_category_memberships table got idx_email_memberships_recipient for exactly this reason. Adding idx_email_deliveries_recipient here would also need the matching statement in schema.py's SCHEMA_STATEMENTS to keep migration/runtime parity, so no single-file fix is offered.
