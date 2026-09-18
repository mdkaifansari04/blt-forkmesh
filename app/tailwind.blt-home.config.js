/** Local Tailwind build for the BLT marketing homepage.

The page previously loaded https://cdn.tailwindcss.com, but the app CSP does
not (and must not) allow that host or unsafe-eval. Build a self-hosted sheet
instead: ``tools/build_blt_home_css.py`` → ``public/blt-home.css``.
*/
module.exports = {
  content: ["./public/index.html"],
  theme: {
    extend: {
      colors: {
        blt: {
          red: "#dc2626",
          coral: "#ef4444",
          ink: "#111827",
        },
      },
      fontFamily: {
        sans: [
          "Geist",
          "Inter",
          "ui-sans-serif",
          "system-ui",
          "-apple-system",
          "BlinkMacSystemFont",
          "Segoe UI",
          "sans-serif",
        ],
        mono: [
          "ui-monospace",
          "SFMono-Regular",
          "Menlo",
          "Monaco",
          "Consolas",
          "monospace",
        ],
      },
    },
  },
};
