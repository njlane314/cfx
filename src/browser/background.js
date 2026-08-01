(function(root, factory) {
  "use strict";

  const api = factory(
    root.chrome, root.fetch, root.TextEncoder, root.TextDecoder, root.AbortController
  );
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    api.listen();
  }
})(globalThis, (chrome, fetch, TextEncoder, TextDecoder, AbortController) => {
  "use strict";

  const localMessage = "cfx-local-request";
  const stateMessage = "cfx-submission-state";
  const fetchStateMessage = "cfx-fetch-state";
  const statePrefix = "cfx:submission:";
  const fetchStatePrefix = "cfx:fetch:";
  const alarmPrefix = "cfx:expire:";
  const stateLifetime = 60000;
  const fetchStateLifetime = 370000;
  const primaryOrigin = "https://codeforces.com";
  const problemOrigins = [
    primaryOrigin,
    "https://m3.codeforces.com",
    "https://mirror.codeforces.com"
  ];
  const problemOriginSet = new Set(problemOrigins);
  const maximumBytes = 16 * 1024 * 1024;
  const smallBody = 64 * 1024;
  const routes = new Map([
    ["ready", {method: "GET", requestBytes: 0, responseBytes: smallBody}],
    ["submission", {method: "GET", requestBytes: 0, responseBytes: maximumBytes}],
    ["fetch", {method: "POST", requestBytes: maximumBytes, responseBytes: smallBody}],
    ["fetch-error", {method: "POST", requestBytes: smallBody, responseBytes: smallBody}],
    ["result", {method: "POST", requestBytes: smallBody, responseBytes: smallBody}]
  ]);
  let stateQueue = Promise.resolve();

  function messageOf(error) {
    const message = error instanceof Error ? error.message : String(error);
    return message.replace(/\s+/g, " ").trim().slice(0, 1000) || "unknown connector error";
  }

  function senderContext(sender) {
    if (
      sender.id !== chrome.runtime.id || sender.frameId !== 0 ||
      !Number.isInteger(sender.tab?.id) || sender.tab.id < 0
    ) {
      throw new Error("connector messages must come from a Codeforces top-level tab");
    }
    let page;
    try {
      page = new URL(sender.url || sender.tab.url || "");
    } catch {
      throw new Error("connector messages must come from a Codeforces top-level tab");
    }
    const primary = page.origin === primaryOrigin;
    const problem = primary
      ? problemPath(page.pathname)
      : problemOriginSet.has(page.origin) && contestProblemPath(page.pathname);
    const result = primary && resultPath(page.pathname);
    if (!problem && !result) {
      throw new Error("connector messages must come from a Codeforces top-level tab");
    }
    return {page, primary, problem, result, tab: sender.tab.id};
  }

  function contestProblemPath(pathname) {
    return /^\/contest\/[0-9]+\/problem\/[^/]+\/?$/i.test(pathname);
  }

  function problemPath(pathname) {
    return (
      contestProblemPath(pathname) ||
      /^\/problemset\/problem\/[0-9]+\/[^/]+\/?$/i.test(pathname)
    );
  }

  function normalizedProblemPath(pathname) {
    return problemPath(pathname) && pathname.endsWith("/") ? pathname.slice(0, -1) : pathname;
  }

  function submissionPath(pathname) {
    return (
      /^\/contest\/[0-9]+\/submit\/?$/i.test(pathname) ||
      /^\/problemset\/submit\/?$/i.test(pathname)
    );
  }

  function resultPath(pathname) {
    return submissionPath(pathname) || (
      /^\/problemset\/status\/?$/i.test(pathname) ||
      /^\/contest\/[0-9]+\/(?:my|status)\/?$/i.test(pathname) ||
      /^\/contest\/[0-9]+\/submission\/[0-9]+\/?$/i.test(pathname) ||
      /^\/problemset\/submission\/[0-9]+\/[0-9]+\/?$/i.test(pathname) ||
      /^\/submissions\/[^/]+\/?$/i.test(pathname) ||
      /^\/enter\/?$/i.test(pathname)
    );
  }

  function routePage(sender, route) {
    return (
      (route === "ready" && (sender.problem || sender.result)) ||
      (route === "fetch" && sender.problem) ||
      (route === "fetch-error" && (sender.problem || sender.result)) ||
      (route === "submission" && sender.primary && submissionPath(sender.page.pathname)) ||
      (route === "result" && sender.result)
    );
  }

  function validatedLocalRequest(message) {
    if (!message || typeof message !== "object" || message.type !== localMessage) {
      throw new Error("invalid connector message");
    }
    const {port, token, route, body = ""} = message;
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
      throw new Error("invalid loopback port");
    }
    if (typeof token !== "string" || !/^[0-9a-f]{64}$/i.test(token)) {
      throw new Error("invalid connector token");
    }
    if (typeof route !== "string" || !routes.has(route)) {
      throw new Error("invalid connector route");
    }
    const policy = routes.get(route);
    if (typeof body !== "string" || new TextEncoder().encode(body).length > policy.requestBytes) {
      throw new Error("connector request is too large");
    }
    return {...policy, port, token, route, body};
  }

  async function limitedResponseBody(response, maximum) {
    const declared = response.headers?.get?.("content-length");
    if (declared != null && (!/^[0-9]+$/.test(declared) || Number(declared) > maximum)) {
      throw new Error("local workbench response is too large");
    }
    if (!response.body?.getReader) {
      const value = await response.text();
      if (new TextEncoder().encode(value).length > maximum) {
        throw new Error("local workbench response is too large");
      }
      return value;
    }

    const reader = response.body.getReader();
    const decoder = new TextDecoder("utf-8", {fatal: true});
    let bytes = 0;
    let value = "";
    while (true) {
      const {done, value: chunk} = await reader.read();
      if (done) break;
      bytes += chunk.byteLength;
      if (bytes > maximum) {
        await reader.cancel();
        throw new Error("local workbench response is too large");
      }
      value += decoder.decode(chunk, {stream: true});
    }
    return value + decoder.decode();
  }

  async function localRequest(message, sender) {
    const request = validatedLocalRequest(message);
    const source = senderContext(sender);
    if (!routePage(source, request.route)) {
      throw new Error("connector route is not allowed from this Codeforces page");
    }
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 10000);
    try {
      const url =
        `http://127.0.0.1:${request.port}/${request.route}/${encodeURIComponent(request.token)}`;
      const response = await fetch(
        url,
        {
          method: request.method,
          headers: {
            "X-Cfx-Extension": chrome.runtime.id,
            ...(request.method === "POST" ? {"Content-Type": "application/json"} : {})
          },
          body: request.method === "POST" ? request.body : undefined,
          cache: "no-store",
          credentials: "omit",
          redirect: "error",
          referrerPolicy: "no-referrer",
          signal: controller.signal
        }
      );
      const body = await limitedResponseBody(response, request.responseBytes);
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

  async function storeState(key, value, lifetime) {
    const expiresAtMillis = Date.now() + lifetime;
    await chrome.storage.session.set({[key]: {...value, expiresAtMillis}});
    try {
      await chrome.alarms.create(alarmName(key), {when: expiresAtMillis});
    } catch (error) {
      await removeStates([key]);
      throw error;
    }
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

  async function statesForTab(prefix, tab, valid, include = () => true) {
    const tabPrefix = `${prefix}${tab}:`;
    const now = Date.now();
    const stored = await chrome.storage.session.get(null);
    const active = [];
    const stale = [];
    for (const [key, value] of Object.entries(stored)) {
      if (!key.startsWith(tabPrefix)) continue;
      const operation = key.slice(tabPrefix.length);
      if (
        !/^[0-9a-f]{64}$/.test(operation) ||
        !valid(value) ||
        !Number.isSafeInteger(value.expiresAtMillis) ||
        value.expiresAtMillis <= now
      ) {
        stale.push(key);
      } else if (include(value)) {
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
    const source = senderContext(sender);
    if (!source.result) {
      throw new Error("submission state is not allowed from this Codeforces page");
    }
    const tab = source.tab;
    if (message.action === "save") {
      const key = stateKey(tab, message.operation);
      if (!validState(message.value)) throw new Error("invalid pending submission");
      if ((await statesForTab(statePrefix, tab, validState)).length) {
        throw new Error("another submission operation is pending in this tab");
      }
      await storeState(key, message.value, stateLifetime);
      return null;
    }
    if (message.action === "load") {
      const states = await statesForTab(statePrefix, tab, validState);
      if (states.length > 1) throw new Error("multiple submission operations are pending in this tab");
      return states[0] || null;
    }
    if (message.action === "remove") {
      await removeStates([stateKey(tab, message.operation)]);
      return null;
    }
    throw new Error("invalid submission-state action");
  }

  function fetchStateKey(tab, operation) {
    if (!/^[0-9a-f]{64}$/.test(operation || "")) {
      throw new Error("invalid fetch operation");
    }
    return `${fetchStatePrefix}${tab}:${operation}`;
  }

  function validFetchState(value) {
    return Boolean(
      value &&
      Number.isInteger(value.port) && value.port > 0 && value.port <= 65535 &&
      typeof value.pathname === "string" && problemPath(value.pathname) &&
      Number.isInteger(value.position) && value.position >= 0 &&
      value.position < problemOrigins.length
    );
  }

  async function fetchStatesForTab(tab, pathname = "") {
    return statesForTab(
      fetchStatePrefix, tab, validFetchState,
      value => !pathname ||
        normalizedProblemPath(value.pathname) === normalizedProblemPath(pathname)
    );
  }

  async function fetchStateNow(message, sender) {
    if (!message || typeof message !== "object" || message.type !== fetchStateMessage) {
      throw new Error("invalid fetch-state message");
    }
    const source = senderContext(sender);
    if (!source.problem) {
      throw new Error("fetch state is not allowed from this Codeforces page");
    }
    const {page, tab} = source;
    if (message.action === "save") {
      const key = fetchStateKey(tab, message.operation);
      if (
        page.origin !== primaryOrigin || !validFetchState(message.value) ||
        normalizedProblemPath(message.value.pathname) !== normalizedProblemPath(page.pathname) ||
        message.value.position !== 0
      ) {
        throw new Error("invalid pending fetch");
      }
      const active = await fetchStatesForTab(tab);
      if (active.some(state => state.operation !== message.operation)) {
        throw new Error("another fetch operation is pending in this tab");
      }
      await storeState(key, {
        ...message.value,
        pathname: normalizedProblemPath(message.value.pathname)
      }, fetchStateLifetime);
      return null;
    }
    if (message.action === "load") {
      const active = await fetchStatesForTab(tab, page.pathname);
      if (active.length > 1) throw new Error("multiple fetch operations are pending in this tab");
      return active[0] || null;
    }
    if (message.action === "advance") {
      const key = fetchStateKey(tab, message.operation);
      const active = await fetchStatesForTab(tab, page.pathname);
      const current = active.find(state => state.operation === message.operation);
      const pagePosition = problemOrigins.indexOf(page.origin);
      const expected = Math.max(current?.value.position ?? -1, pagePosition) + 1;
      if (!current || !Number.isInteger(message.position) ||
          message.position !== expected || message.position >= problemOrigins.length) {
        throw new Error("invalid fetch progression");
      }
      const value = {...current.value, position: message.position};
      await chrome.storage.session.set({[key]: value});
      return {operation: message.operation, value};
    }
    if (message.action === "remove") {
      const key = fetchStateKey(tab, message.operation);
      await removeStates([key]);
      return null;
    }
    throw new Error("invalid fetch-state action");
  }

  function serializeState(operation) {
    const result = stateQueue.then(operation);
    stateQueue = result.catch(() => {});
    return result;
  }

  function submissionState(message, sender) {
    return serializeState(() => submissionStateNow(message, sender));
  }

  function fetchState(message, sender) {
    return serializeState(() => fetchStateNow(message, sender));
  }

  function expireState(alarm) {
    return serializeState(async () => {
      if (typeof alarm?.name !== "string" || !alarm.name.startsWith(alarmPrefix)) return;
      const key = alarm.name.slice(alarmPrefix.length);
      if (!/^cfx:(?:submission|fetch):[0-9]+:[0-9a-f]{64}$/.test(key)) return;
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
          : message?.type === fetchStateMessage
            ? fetchState
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
    fetchStateKey,
    listen,
    limitedResponseBody,
    stateIdle: () => stateQueue,
    stateKey
  };
});
