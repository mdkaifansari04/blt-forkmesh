// Optional, coarse product analytics.
//
// This file is included by every public page, but it does not load PostHog or
// send a network request unless this browser has explicitly opted in. ForkMesh
// never sends a raw path, query string, referrer, element text, form value, or
// account/repository identifier through this integration.
(() => {
  "use strict";

  const CONSENT_KEY = "forkmesh.analyticsConsent.v1";
  const PROJECT_KEY = "phc_AJQ22WskEYjbe5Qb8VTDczA5KkEQ33aXqShiPo9GdENj";
  const API_HOST = "https://b.forkmesh.com";
  const UI_HOST = "https://us.posthog.com";
  const CLIENT_ERROR_ENDPOINT = "/api/client-errors";
  const CLIENT_ERROR_WINDOW_MS = 60_000;
  const CLIENT_ERROR_MAX_PER_WINDOW = 8;
  let started = false;
  let clientErrorWindowStartedAt = 0;
  let clientErrorWindowCount = 0;
  const recentClientErrors = new Map();

  function privacySignalEnabled() {
    const dnt = String(
      navigator.doNotTrack ||
        window.doNotTrack ||
        navigator.msDoNotTrack ||
        "",
    ).toLowerCase();
    return navigator.globalPrivacyControl === true || dnt === "1" || dnt === "yes";
  }

  function storedConsent() {
    try {
      return localStorage.getItem(CONSENT_KEY) || "unset";
    } catch (_) {
      return "unset";
    }
  }

  function saveConsent(value) {
    try {
      localStorage.setItem(CONSENT_KEY, value);
      return true;
    } catch (_) {
      return false;
    }
  }

  // Deliberately map paths to a small fixed vocabulary. In particular,
  // /owner/repository, private deep links, issue ids, searches, and query
  // parameters never leave the browser.
  function coarseSurface(pathname) {
    const path = String(pathname || "/").toLowerCase();
    if (path === "/") return "home";
    if (path === "/homev2" || path === "/homev2/") return "home-v2";
    if (path === "/world" || path.startsWith("/world/")) return "world";
    if (path === "/dashboard" || path.startsWith("/dashboard/")) return "dashboard";
    if (path === "/docs" || path.startsWith("/docs/")) return "documentation";
    if (path === "/blog" || path.startsWith("/blog/")) return "blog";
    if (path === "/network" || path.startsWith("/network/")) return "network";
    if (path === "/chat" || path.startsWith("/chat/")) return "chat";
    if (
      path === "/login" ||
      path === "/signup" ||
      path === "/forgot-password" ||
      path === "/reset-password"
    ) {
      return "account";
    }
    const parts = path.split("/").filter(Boolean);
    if (parts.length >= 2) return "repository";
    return "public-page";
  }

  function redactClientError(value, maximum = 500) {
    return String(value || "")
      .replace(
        /\b(?:authorization|bearer|password|passwd|secret|token|api[_-]?key)\b\s*[:=]?\s*[^\s,;]+/gi,
        "[redacted-secret]",
      )
      .replace(
        /\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b/gi,
        "[redacted-email]",
      )
      .replace(/\b[A-Za-z0-9_-]{32,}\b/g, "[redacted-id]")
      .replace(/\bhttps?:\/\/[^\s)]+/gi, "[redacted-url]")
      .replace(
        /\/(?:[A-Za-z0-9._~%-]+\/)+[A-Za-z0-9._~%-]+(?:[?#][^\s]*)?/g,
        "/[redacted-path]",
      )
      .replace(/[\u0000-\u001f\u007f]+/g, " ")
      .replace(/\s+/g, " ")
      .trim()
      .slice(0, maximum);
  }

  function clientErrorSource(value) {
    const withoutQuery = String(value || "").split(/[?#]/, 1)[0];
    const source = withoutQuery.split("/").filter(Boolean).pop() || "";
    return source.replace(/[^A-Za-z0-9._-]+/g, "").slice(0, 80);
  }

  function reportClientError(kind, error, fallbackMessage = "", source = "", line = 0, column = 0) {
    const now = Date.now();
    if (
      !clientErrorWindowStartedAt ||
      now - clientErrorWindowStartedAt >= CLIENT_ERROR_WINDOW_MS
    ) {
      clientErrorWindowStartedAt = now;
      clientErrorWindowCount = 0;
    }
    if (clientErrorWindowCount >= CLIENT_ERROR_MAX_PER_WINDOW) return;
    const message = redactClientError(
      error?.message || fallbackMessage || "Unspecified browser exception",
      500,
    );
    const stack = redactClientError(
      String(error?.stack || "")
        .split("\n")
        .slice(0, 8)
        .join("\n"),
      700,
    );
    const surface = coarseSurface(location.pathname);
    const signature = `${kind}|${surface}|${message}|${stack.slice(0, 180)}`;
    const previous = recentClientErrors.get(signature) || 0;
    if (now - previous < CLIENT_ERROR_WINDOW_MS) return;
    recentClientErrors.set(signature, now);
    for (const [key, seenAt] of recentClientErrors) {
      if (now - seenAt >= CLIENT_ERROR_WINDOW_MS) recentClientErrors.delete(key);
    }
    clientErrorWindowCount += 1;
    fetch(CLIENT_ERROR_ENDPOINT, {
      method: "POST",
      credentials: "same-origin",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        kind,
        surface,
        message,
        stack,
        source: clientErrorSource(source),
        line: Math.max(0, Number(line) || 0),
        column: Math.max(0, Number(column) || 0),
      }),
      keepalive: true,
    }).catch(() => {});
  }

  window.addEventListener(
    "error",
    (event) => {
      // Resource-load errors do not expose a useful exception and can reveal
      // asset paths. The operational collector is for uncaught JavaScript.
      if (event.target && event.target !== window) return;
      reportClientError(
        "error",
        event.error,
        event.message,
        event.filename,
        event.lineno,
        event.colno,
      );
    },
    true,
  );
  window.addEventListener("unhandledrejection", (event) => {
    const reason = event.reason;
    // World navigation and mobile gesture reconciliation intentionally abort
    // stale fetches. Browsers surface those cancellations as unhandled
    // AbortError rejections even though no request or UI action failed.
    if (
      reason?.name === "AbortError" ||
      /signal is aborted|operation was aborted/i.test(
        String(reason?.message || ""),
      )
    ) {
      return;
    }
    reportClientError(
      "unhandledrejection",
      reason instanceof Error ? reason : null,
      typeof reason === "string" ? reason : "Unhandled promise rejection",
    );
  });

  function installPostHogBootstrap() {
    // Standard queueing bootstrap, intentionally invoked only after consent.
    !function(t,e){var o,n,p,r;e.__SV||(window.posthog&&window.posthog.__loaded)||(window.posthog=e,e._i=[],e.init=function(i,s,a){function g(t,e){var o=e.split(".");2==o.length&&(t=t[o[0]],e=o[1]),t[e]=function(){t.push([e].concat(Array.prototype.slice.call(arguments,0)))}}(p=t.createElement("script")).type="text/javascript",p.crossOrigin="anonymous",p.async=!0,p.src=s.api_host.replace(".i.posthog.com","-assets.i.posthog.com")+"/static/array.js",(r=t.getElementsByTagName("script")[0]).parentNode.insertBefore(p,r);var u=e;for(void 0!==a?u=e[a]=[]:a="posthog",u.people=u.people||[],u.toString=function(t){var e="posthog";return"posthog"!==a&&(e+="."+a),t||(e+=" (stub)"),e},u.people.toString=function(){return u.toString(1)+".people (stub)"},o="ki Ci init qi Hi pr Bi zi Di capture calculateEventProperties Qi register register_once register_for_session unregister unregister_for_session Ki getFeatureFlag getFeatureFlagPayload getFeatureFlagResult getAllFeatureFlags isFeatureEnabled reloadFeatureFlags updateFlags updateEarlyAccessFeatureEnrollment getEarlyAccessFeatures on onFeatureFlags onSurveysLoaded onSessionId getSurveys getActiveMatchingSurveys renderSurvey displaySurvey cancelPendingSurvey canRenderSurvey canRenderSurveyAsync Xi identify setPersonProperties unsetPersonProperties group resetGroups setPersonPropertiesForFlags resetPersonPropertiesForFlags setGroupPropertiesForFlags resetGroupPropertiesForFlags reset shutdown setIdentity clearIdentity get_distinct_id getGroups get_session_id get_session_replay_url alias set_config startSessionRecording stopSessionRecording sessionRecordingStarted captureException addExceptionStep captureLog startExceptionAutocapture stopExceptionAutocapture loadToolbar get_property getSessionProperty Ji Gi createPersonProfile setInternalOrTestUser Yi Ai rn opt_in_capturing opt_out_capturing has_opted_in_capturing has_opted_out_capturing get_explicit_consent_status is_capturing clear_opt_in_out_capturing Vi debug mr it getPageViewId captureTraceFeedback captureTraceMetric Oi".split(" "),n=0;n<o.length;n++)g(u,o[n]);e._i.push([i,s,a])},e.__SV=1)}(document,window.posthog||[]);
  }

  function start() {
    if (started || storedConsent() !== "granted" || privacySignalEnabled()) {
      return false;
    }
    started = true;
    installPostHogBootstrap();
    window.posthog.init(PROJECT_KEY, {
      api_host: API_HOST,
      ui_host: UI_HOST,
      defaults: "2026-05-30",
      autocapture: false,
      capture_pageview: false,
      capture_pageleave: false,
      capture_dead_clicks: false,
      capture_exceptions: false,
      capture_heatmaps: false,
      capture_performance: false,
      rageclick: false,
      disable_session_recording: true,
      disable_surveys: true,
      advanced_disable_flags: true,
      disable_persistence: true,
      persistence: "memory",
      person_profiles: "identified_only",
      respect_dnt: true,
      mask_all_text: true,
      mask_all_element_attributes: true,
      before_send(event) {
        if (!event || event.event !== "forkmesh_surface_view") return null;
        return {
          ...event,
          properties: {
            surface: coarseSurface(location.pathname),
            consent: "explicit",
            $process_person_profile: false,
          },
        };
      },
    });
    window.posthog.capture("forkmesh_surface_view", {
      surface: coarseSurface(location.pathname),
      consent: "explicit",
      $process_person_profile: false,
    });
    return true;
  }

  function stop() {
    if (window.posthog && typeof window.posthog.opt_out_capturing === "function") {
      window.posthog.opt_out_capturing();
    }
    if (window.posthog && typeof window.posthog.shutdown === "function") {
      window.posthog.shutdown();
    }
    started = false;
  }

  function setConsent(granted) {
    if (granted && privacySignalEnabled()) {
      saveConsent("denied");
      stop();
      window.dispatchEvent(
        new CustomEvent("forkmesh:analytics-consent", {
          detail: { status: "blocked-by-privacy-signal" },
        }),
      );
      return "blocked-by-privacy-signal";
    }
    const status = granted ? "granted" : "denied";
    saveConsent(status);
    if (granted) start();
    else stop();
    window.dispatchEvent(
      new CustomEvent("forkmesh:analytics-consent", {
        detail: { status },
      }),
    );
    return status;
  }

  function status() {
    if (privacySignalEnabled()) return "blocked-by-privacy-signal";
    return storedConsent();
  }

  window.ForkMeshAnalytics = Object.freeze({
    setConsent,
    status,
  });

  // Default is off. This is the sole automatic start path and it requires a
  // previously stored explicit grant on this device.
  if (storedConsent() === "granted" && !privacySignalEnabled()) start();
})();
