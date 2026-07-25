(() => {
  "use strict";

  const messageType = "cfprobs-local-request";
  const routes = new Set(["ready", "submission", "fetch", "fetch-error", "result"]);
  const maximumBodyBytes = 16 * 1024 * 1024;
  const maximumResponseBytes = 16 * 1024 * 1024;

  function messageOf(error) {
    const message = error instanceof Error ? error.message : String(error);
    return message.replace(/\s+/g, " ").trim().slice(0, 1000) || "unknown connector error";
  }

  function codeforcesPage(sender) {
    if (sender.id !== chrome.runtime.id || sender.frameId !== 0) {
      return false;
    }
    let url;
    try {
      url = new URL(sender.url || sender.tab?.url || "");
    } catch {
      return false;
    }
    if (url.origin !== "https://codeforces.com") {
      return false;
    }
    return (
      /^\/contest\/[0-9]+\/problem\/[^/]+\/?$/i.test(url.pathname) ||
      /^\/problemset\/problem\/[0-9]+\/[^/]+\/?$/i.test(url.pathname) ||
      /^\/contest\/[0-9]+\/submit\/?$/i.test(url.pathname) ||
      /^\/problemset\/submit\/?$/i.test(url.pathname) ||
      /^\/enter\/?$/i.test(url.pathname)
    );
  }

  function validatedRequest(message) {
    if (!message || typeof message !== "object" || message.type !== messageType) {
      throw new Error("invalid connector message");
    }
    const {port, token, route, method = "GET", body = ""} = message;
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
      throw new Error("invalid loopback port");
    }
    if (typeof token !== "string" || !/^[0-9a-f]{64}$/i.test(token)) {
      throw new Error("invalid connector token");
    }
    if (typeof route !== "string" || !routes.has(route)) {
      throw new Error("invalid connector route");
    }
    if (method !== "GET" && method !== "POST") {
      throw new Error("invalid connector method");
    }
    if (typeof body !== "string" || new TextEncoder().encode(body).length > maximumBodyBytes) {
      throw new Error("connector request is too large");
    }
    if (method === "GET" && body.length !== 0) {
      throw new Error("GET connector request has a body");
    }
    return {port, token, route, method, body};
  }

  async function localRequest(message, sender) {
    if (!codeforcesPage(sender)) {
      throw new Error("connector messages must come from a Codeforces top-level page");
    }
    const request = validatedRequest(message);
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 10000);
    try {
      const response = await fetch(
        `http://127.0.0.1:${request.port}/${request.route}/${encodeURIComponent(request.token)}`,
        {
          method: request.method,
          headers: {
            "X-Cfprobs-Extension": chrome.runtime.id,
            ...(request.method === "POST"
              ? {"Content-Type": "text/plain;charset=UTF-8"}
              : {})
          },
          body: request.method === "POST" ? request.body : undefined,
          cache: "no-store",
          credentials: "omit",
          referrerPolicy: "no-referrer",
          signal: controller.signal
        }
      );
      const body = await response.text();
      if (new TextEncoder().encode(body).length > maximumResponseBytes) {
        throw new Error("local workbench response is too large");
      }
      return {status: response.status, body};
    } finally {
      clearTimeout(timeout);
    }
  }

  chrome.runtime.onMessage.addListener((message, sender, respond) => {
    if (!message || message.type !== messageType) {
      return false;
    }
    localRequest(message, sender).then(
      response => respond({ok: true, ...response}),
      error => respond({ok: false, error: messageOf(error)})
    );
    return true;
  });
})();
