(() => {
  const $ = (sel) => document.querySelector(sel);
  const hint = $("#forgot-hint");
  const btn = $("#forgot-btn");
  const input = $("#identifier");

  function setHint(text, cls) {
    hint.textContent = text || "";
    hint.className = "hint" + (cls ? " " + cls : "");
  }

  // Deliberately generic: whether or not the identifier matches an account, we
  // show the same confirmation so this page can't be used to probe which emails
  // are registered (the API returns {ok:true} either way).
  const SENT_MESSAGE =
    "If that account exists, a reset link is on its way. Check your email.";

  async function submit() {
    const identifier = input.value.trim();
    if (!identifier) {
      setHint("Enter the email for your account.", "bad");
      return;
    }
    btn.disabled = true;
    btn.textContent = "Sending…";
    let res;
    try {
      res = await fetch("/api/accounts/forgot-password", {
        method: "POST",
        headers: { "content-type": "application/json", accept: "application/json" },
        body: JSON.stringify({ identifier }),
      });
    } catch (_) {
      btn.disabled = false;
      btn.textContent = "Send reset link";
      setHint("Network error - please try again.", "bad");
      return;
    }
    btn.disabled = false;
    btn.textContent = "Send reset link";
    if (res.ok) {
      setHint(SENT_MESSAGE, "good");
      input.value = "";
      return;
    }
    setHint("Could not send a reset link. Please try again.", "bad");
  }

  btn.addEventListener("click", submit);
  input.addEventListener("keydown", (e) => { if (e.key === "Enter") submit(); });
})();
