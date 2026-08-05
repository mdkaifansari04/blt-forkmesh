// Starts a server-created Polar checkout without exposing provider credentials
// or product ids to the browser.
(() => {
  function sessionToken() {
    try {
      const session = JSON.parse(localStorage.getItem("forkmesh.session") || "null");
      return String(session?.sessionToken || "");
    } catch (_) {
      return "";
    }
  }

  function setStatus(message, bad = false) {
    const status = document.querySelector("[data-polar-checkout-status]");
    if (!status) return;
    status.textContent = message || "";
    status.className = "min-h-5 text-center text-[0.74rem] "
      + (bad ? "text-red-300" : "text-white/72");
  }

  async function beginCheckout(button) {
    const token = sessionToken();
    if (!token) {
      location.assign("/login?next=" + encodeURIComponent("/pricing#memberships"));
      return;
    }
    const buttons = document.querySelectorAll("[data-polar-checkout]");
    buttons.forEach((item) => { item.disabled = true; });
    setStatus("Opening secure checkout…");
    try {
      const response = await fetch("/api/integrations/polar/checkout", {
        method: "POST",
        headers: {
          accept: "application/json",
          "content-type": "application/json",
          ...(token !== "cookie" ? { authorization: "Bearer " + token } : {}),
        },
        credentials: "same-origin",
        cache: "no-store",
        body: JSON.stringify({
          tier: button.dataset.polarCheckout,
          ...(token !== "cookie" ? { sessionToken: token } : {}),
        }),
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || !data.url) throw new Error(data.error || "checkout_failed");
      location.assign(data.url);
    } catch (error) {
      setStatus(
        error.message === "polar_not_configured"
          ? "Membership checkout is not configured on this deployment yet."
          : "Checkout could not be opened. Please try again.",
        true,
      );
      buttons.forEach((item) => { item.disabled = false; });
    }
  }

  document.addEventListener("click", (event) => {
    const button = event.target.closest("[data-polar-checkout]");
    if (button) void beginCheckout(button);
  });
})();
