# name: Write the next changelog blog post
# description: Publish a polished product update from everything landed since the previous changelog post, with original artwork matching the blog.
# agent: codex
# model: gpt-5.6-sol

Publish the next ForkMesh changelog blog post as a complete, production-ready change to this repository.

First establish the reporting window from repository evidence. Find the newest changelog-style product update in `cloudflare_worker/public/blog.html` (currently the newest dated card in the Product updates section), locate the commit that published it, and inspect every meaningful change from that publication commit through `HEAD`. Use Git history and diffs, merged pull-request records, closed issues, releases, and relevant documentation as evidence. Do not infer shipped work from titles alone, include changes already covered by the previous post, or turn the article into a raw commit list. Group the important user-facing progress into a coherent story and be precise about what is live versus experimental or still in progress.

Then implement the post in the existing blog, not as a standalone Markdown draft:

- Follow the article structure, typography, metadata, social-sharing block, and tone of the latest product-update posts under `cloudflare_worker/public/blog/`.
- Choose a concise editorial title, slug, lede, description, publication date, and honest reading time. Write clear, warm prose with useful technical specificity and strong section headings. Avoid hype, filler, and repetitive “we added” phrasing.
- Create an original 16:9 hero image that visually summarizes this reporting window and matches the blog's existing cinematic, nocturnal, green/cyan ForkMesh illustration language. Use the image-generation capability available to you and inspect the existing files in `cloudflare_worker/public/assets/blog/features/` as visual references. Do not reuse an old hero, paste UI screenshots together, or imitate a third-party copyrighted work. Optimize the final asset as WebP and save it at `cloudflare_worker/public/assets/blog/features/<slug>.webp` with accurate alt text and social image dimensions.
- Add the complete article at `cloudflare_worker/public/blog/<slug>/index.html` and integrate it as the newest Product updates card and search result in `cloudflare_worker/public/blog.html`. Keep dates, canonical/Open Graph/Twitter metadata, image paths, excerpts, keywords, previous/next navigation, RSS parsing conventions, and social placeholders consistent with the existing site.
- Update the former newest post's next/previous navigation where needed so readers can move chronologically between posts. Preserve unrelated cards and feature articles.
- Extend any count- or content-sensitive blog tests so the new post, image, index entry, and navigation are covered.

Before finishing, inspect the rendered page at desktop and mobile widths if browser tooling is available, run the focused blog frontend/RSS/social tests, and correct any failures. Report the evidence window you used, the story themes you chose, the files created or updated, image dimensions/format, and test results.
