(function(root, factory) {
  "use strict";

  const api = factory(root.chrome, root.fetch, root.TextEncoder, root.AbortController);
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    api.listen();
  }
})(globalThis, (chrome, fetch, TextEncoder, AbortController) => {
  "use strict";

  const localMessage = "cfx-local-request";
  const stateMessage = "cfx-submission-state";
  const statePrefix = "cfx:submission:";
  const alarmPrefix = "cfx:expire:";
  const stateLifetime = 60000;
  const routes = new Set(["ready", "submission", "fetch", "fetch-error", "result"]);
  const maximumBytes = 16 * 1024 * 1024;
  let stateQueue = Promise.resolve();

  function messageOf(error) {
    const message = error instanceof Error ? error.message : String(error);
    return message.replace(/\s+/g, " ").trim().slice(0, 1000) || "unknown connector error";
  }

  function codeforcesPage(sender) {
    if (sender.id !== chrome.runtime.id || sender.frameId !== 0) return false;
    let url;
    try {
      url = new URL(sender.url || sender.tab?.url || "");
    } catch {
      return false;
    }
    if (url.origin !== "https://codeforces.com") return false;
    return (
      /^\/contest\/[0-9]+\/problem\/[^/]+\/?$/i.test(url.pathname) ||
      /^\/problemset\/problem\/[0-9]+\/[^/]+\/?$/i.test(url.pathname) ||
      /^\/contest\/[0-9]+\/submit\/?$/i.test(url.pathname) ||
      /^\/problemset\/submit\/?$/i.test(url.pathname) ||
      /^\/problemset\/status\/?$/i.test(url.pathname) ||
      /^\/contest\/[0-9]+\/(?:my|status)\/?$/i.test(url.pathname) ||
      /^\/contest\/[0-9]+\/submission\/[0-9]+\/?$/i.test(url.pathname) ||
      /^\/problemset\/submission\/[0-9]+\/[0-9]+\/?$/i.test(url.pathname) ||
      /^\/submissions\/[^/]+\/?$/i.test(url.pathname) ||
      /^\/enter\/?$/i.test(url.pathname)
    );
  }

  function tabId(sender) {
    if (!codeforcesPage(sender) || !Number.isInteger(sender.tab?.id) || sender.tab.id < 0) {
      throw new Error("connector messages must come from a Codeforces top-level tab");
    }
    return sender.tab.id;
  }

  function validatedLocalRequest(message) {
    if (!message || typeof message !== "object" || message.type !== localMessage) {
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
    if (method !== "GET" && method !== "POST") throw new Error("invalid connector method");
    if (typeof body !== "string" || new TextEncoder().encode(body).length > maximumBytes) {
      throw new Error("connector request is too large");
    }
    if (method === "GET" && body) throw new Error("GET connector request has a body");
    return {port, token, route, method, body};
  }

  async function localRequest(message, sender) {
    tabId(sender);
    const request = validatedLocalRequest(message);
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 10000);
    try {
      const response = await fetch(
        `http://127.0.0.1:${request.port}/${request.route}/${encodeURIComponent(request.token)}`,
        {
          method: request.method,
          headers: {
            "X-Cfx-Extension": chrome.runtime.id,
            ...(request.method === "POST" ? {"Content-Type": "application/json"} : {})
          },
          body: request.method === "POST" ? request.body : undefined,
          cache: "no-store",
          credentials: "omit",
          referrerPolicy: "no-referrer",
          signal: controller.signal
        }
      );
      const body = await response.text();
      if (new TextEncoder().encode(body).length > maximumBytes) {
        throw new Error("local workbench response is too large");
      }
      return {status: response.status, body};
    } finally {
      clearTimeout(timeout);
    }
  }

  function stateKey(tab, operation) {
    if (!/^[0-9a-f]{64}$/i.test(operation || "")) {
      throw new Error("invalid submission operation");
    }
    return `${statePrefix}${tab}:${operation.toLowerCase()}`;
  }

  function alarmName(key) {
    return `${alarmPrefix}${key}`;
  }

  async function removeStates(keys) {
    if (!keys.length) return;
    await chrome.storage.session.remove(keys);
    await Promise.all(keys.map(key => chrome.alarms.clear(alarmName(key))));
  }

  function validState(value) {
    return Boolean(
      value &&
      Number.isInteger(value.port) && value.port > 0 && value.port <= 65535 &&
      typeof value.target === "string" && /^[0-9]+[A-Za-z][A-Za-z0-9]*$/.test(value.target) &&
      typeof value.handle === "string" && /^[A-Za-z0-9_.-]+$/.test(value.handle) &&
      Array.isArray(value.previousIds) && value.previousIds.length <= 100 &&
      value.previousIds.every(id => typeof id === "string" && /^[0-9]+$/.test(id)) &&
      new Set(value.previousIds).size === value.previousIds.length &&
      Number.isSafeInteger(value.submittedAtMillis) && value.submittedAtMillis > 0 &&
      Number.isSafeInteger(value.lastApiRequestMillis) && value.lastApiRequestMillis > 0
    );
  }

  async function statesForTab(tab, now = Date.now()) {
    const prefix = `${statePrefix}${tab}:`;
    const stored = await chrome.storage.session.get(null);
    const active = [];
    const stale = [];
    for (const [key, value] of Object.entries(stored)) {
      if (!key.startsWith(prefix)) continue;
      const operation = key.slice(prefix.length);
      if (
        !/^[0-9a-f]{64}$/.test(operation) ||
        !validState(value) ||
        !Number.isSafeInteger(value.expiresAtMillis) ||
        value.expiresAtMillis <= now
      ) {
        stale.push(key);
      } else {
        active.push({operation, value});
      }
    }
    await removeStates(stale);
    return active;
  }

  async function submissionStateNow(message, sender) {
    if (!message || typeof message !== "object" || message.type !== stateMessage) {
      throw new Error("invalid submission-state message");
    }
    const tab = tabId(sender);
    if (message.action === "save") {
      const key = stateKey(tab, message.operation);
      if (!validState(message.value)) throw new Error("invalid pending submission");
      if ((await statesForTab(tab)).length) {
        throw new Error("another submission operation is pending in this tab");
      }
      const expiresAtMillis = Date.now() + stateLifetime;
      await chrome.storage.session.set({[key]: {...message.value, expiresAtMillis}});
      try {
        await chrome.alarms.create(alarmName(key), {when: expiresAtMillis});
      } catch (error) {
        await removeStates([key]);
        throw error;
      }
      return null;
    }
    if (message.action === "load") {
      const states = await statesForTab(tab);
      if (states.length > 1) throw new Error("multiple submission operations are pending in this tab");
      return states[0] || null;
    }
    if (message.action === "remove") {
      await removeStates([stateKey(tab, message.operation)]);
      return null;
    }
    throw new Error("invalid submission-state action");
  }

  function serializeState(operation) {
    const result = stateQueue.then(operation);
    stateQueue = result.catch(() => {});
    return result;
  }

  function submissionState(message, sender) {
    return serializeState(() => submissionStateNow(message, sender));
  }

  function expireState(alarm) {
    return serializeState(async () => {
      if (typeof alarm?.name !== "string" || !alarm.name.startsWith(alarmPrefix)) return;
      const key = alarm.name.slice(alarmPrefix.length);
      if (!/^cfx:submission:[0-9]+:[0-9a-f]{64}$/.test(key)) return;
      await removeStates([key]);
    });
  }

  function listen() {
    chrome.alarms.onAlarm.addListener(alarm => { void expireState(alarm); });
    chrome.runtime.onMessage.addListener((message, sender, respond) => {
      const handler = message?.type === localMessage
        ? localRequest
        : message?.type === stateMessage
          ? submissionState
          : null;
      if (!handler) return false;
      handler(message, sender).then(
        value => respond(message.type === localMessage ? {ok: true, ...value} : {ok: true, value}),
        error => respond({ok: false, error: messageOf(error)})
      );
      return true;
    });
  }

  return {
    alarmName,
    codeforcesPage,
    expireState,
    listen,
    localRequest,
    stateIdle: () => stateQueue,
    stateKey,
    statesForTab,
    submissionState
  };
});
