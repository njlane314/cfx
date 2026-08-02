(function(root, factory) {
  "use strict";

  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  else api.createConnector(root).launch();
})(globalThis, () => {
  "use strict";

  const samples = (() => {
  function cleanUrl(location) {
    const url = new URL(location.href);
    url.hash = "";
    if (/^(?:m3|mirror)\.codeforces\.com$/i.test(url.hostname)) {
      url.hostname = "codeforces.com";
      url.search = "";
    }
    return url.href;
  }

  function problemIndex(pathname) {
    const match =
      pathname.match(/^\/contest\/[0-9]+\/problem\/([^/]+)/i) ||
      pathname.match(/^\/problemset\/problem\/[0-9]+\/([^/]+)/i);
    return match ? decodeURIComponent(match[1]).toUpperCase() : "";
  }

  function parseTimeLimit(text) {
    const match = text
      .replace(",", ".")
      .match(/([0-9]+(?:\.[0-9]+)?)\s*(milliseconds?|ms|seconds?|s)\b/i);
    if (!match) throw new Error("cannot read the time limit");
    const milliseconds =
      Number(match[1]) * (/^(?:milliseconds?|ms)$/i.test(match[2]) ? 1 : 1000);
    if (!Number.isFinite(milliseconds) || milliseconds <= 0 || milliseconds > 2147483647) {
      throw new Error("invalid time limit");
    }
    return Math.round(milliseconds);
  }

  function parseMemoryLimit(text) {
    const match = text
      .replace(",", ".")
      .match(/([0-9]+(?:\.[0-9]+)?)\s*(kilobytes?|kb|megabytes?|mb|gigabytes?|gb)\b/i);
    if (!match) throw new Error("cannot read the memory limit");
    const unit = match[2].toLowerCase();
    const megabytes = Number(match[1]) *
      (unit.startsWith("g") ? 1024 : unit.startsWith("k") ? 1 / 1024 : 1);
    if (!Number.isFinite(megabytes) || megabytes <= 0 || megabytes > 2147483647) {
      throw new Error("invalid memory limit");
    }
    return Math.round(megabytes);
  }

  function renderedSample(pre) {
    const text = typeof pre.innerText === "string"
      ? pre.innerText
      : (() => {
          const copy = pre.cloneNode(true);
          for (const lineBreak of copy.querySelectorAll("br")) lineBreak.replaceWith("\n");
          return copy.textContent || "";
        })();
    return text.replace(/\r\n?/g, "\n");
  }

  function extractProblem(document, location) {
    const statement = document.querySelector(".problem-statement");
    if (!statement) throw new Error("Codeforces problem statement is unavailable");
    const title = statement.querySelector(".header .title");
    const time = statement.querySelector(".header .time-limit");
    const memory = statement.querySelector(".header .memory-limit");
    if (!title || !time || !memory) {
      throw new Error("Codeforces problem metadata is incomplete");
    }

    const index = problemIndex(location.pathname);
    let name = (title.textContent || "").replace(/\s+/g, " ").trim();
    if (index && name.toUpperCase().startsWith(`${index}.`)) {
      name = name.slice(index.length + 1).trim();
    }
    if (!name) throw new Error("Codeforces problem has no title");

    const inputs = Array.from(statement.querySelectorAll(".sample-test .input pre"));
    const outputs = Array.from(statement.querySelectorAll(".sample-test .output pre"));
    if (!inputs.length || inputs.length !== outputs.length) {
      throw new Error("Codeforces sample input/output pairs are incomplete");
    }
    return {
      name,
      url: cleanUrl(location),
      timeLimit: parseTimeLimit(time.textContent || ""),
      memoryLimit: parseMemoryLimit(memory.textContent || ""),
      tests: inputs.map((input, position) => ({
        input: renderedSample(input),
        output: renderedSample(outputs[position])
      }))
    };
  }

  return {cleanUrl, extractProblem, parseMemoryLimit, parseTimeLimit, problemIndex, renderedSample};
  })();

  const submission = (() => {
  function targetParts(target) {
    const match = String(target).match(/^([0-9]+)([A-Za-z][A-Za-z0-9]*)$/);
    if (!match) throw new Error("invalid submission target");
    return {contest: match[1], index: match[2].toUpperCase()};
  }

  function publicUrl(value, origin) {
    const url = new URL(value, origin);
    if (url.origin !== origin) throw new Error("Codeforces redirected outside its own origin");
    url.search = "";
    url.hash = "";
    return url.href;
  }

  function validateArtifact(value) {
    if (!value || typeof value !== "object") {
      throw new Error("local workbench returned an invalid submission");
    }
    for (const field of ["target", "index", "language", "source"]) {
      if (typeof value[field] !== "string") throw new Error(`local submission has no ${field}`);
    }
    const target = targetParts(value.target);
    if (
      !/^[A-Za-z][A-Za-z0-9]*$/.test(value.index) ||
      target.index !== value.index.toUpperCase() ||
      !value.source
    ) {
      throw new Error("local submission fields are invalid");
    }
    return value;
  }

  function profileHandle(link, origin) {
    try {
      const url = new URL(link.href, origin);
      if (url.origin !== origin || url.username || url.password) return "";
      const match = url.pathname.match(/^\/profile\/([^/]+)\/?$/i);
      return match ? decodeURIComponent(match[1]) : "";
    } catch {
      return "";
    }
  }

  function loggedInHandle(document, origin) {
    const links = document.querySelectorAll(
      '#header a[href*="/profile/"], .lang-chooser a[href*="/profile/"]'
    );
    const handles = new Map();
    for (const link of links) {
      const handle = profileHandle(link, origin);
      if (/^[A-Za-z0-9_.-]{1,64}$/.test(handle)) handles.set(handle.toLowerCase(), handle);
    }
    if (handles.size !== 1) {
      throw new Error(
        handles.size ? "cannot determine the signed-in Codeforces handle" : "Codeforces is not signed in"
      );
    }
    return handles.values().next().value;
  }

  function signedOut(document) {
    return Boolean(document.querySelector('a[href^="/enter"], form[action*="/enter"]'));
  }

  function pageError(document, location) {
    if (signedOut(document)) {
      return "Codeforces is not signed in in Chrome; sign in, then rerun cfx submit";
    }
    if (
      !/^\/contest\/[0-9]+\/submit\/?$/i.test(location.pathname) &&
      !/^\/problemset\/submit\/?$/i.test(location.pathname)
    ) {
      return "Codeforces redirected away from its submission page; reload Codeforces, then rerun cfx submit";
    }
    return "";
  }

  function dispatchChange(element, EventClass) {
    element.dispatchEvent(new EventClass("input", {bubbles: true}));
    element.dispatchEvent(new EventClass("change", {bubbles: true}));
  }

  function selectProblem(select, index, EventClass) {
    const option = Array.from(select.options).find(
      candidate => candidate.value.toUpperCase() === index.toUpperCase()
    );
    if (!option) throw new Error(`problem ${index} is not available on this submission page`);
    select.value = option.value;
    dispatchChange(select, EventClass);
  }

  function normalizedLanguage(value) {
    return value.toLowerCase().replace(/\+\+/g, "pp").replace(/[^a-z0-9]+/g, "");
  }

  function cppStandard(value) {
    const match = value.toLowerCase().match(/(?:gnu\s*)?(?:g\+\+|c\+\+|cpp)\D*([0-9]{2})/);
    return match ? match[1] : "";
  }

  function selectLanguage(select, requested, EventClass) {
    const wanted = String(requested);
    const normalized = normalizedLanguage(wanted);
    const standard = cppStandard(wanted);
    const options = Array.from(select.options).filter(option => !option.disabled);
    const option =
      options.find(candidate => candidate.value === wanted) ||
      options.find(candidate => normalizedLanguage(candidate.textContent || "") === normalized) ||
      options.find(candidate => {
        const label = normalizedLanguage(candidate.textContent || "");
        return normalized.length > 2 && (label.includes(normalized) || normalized.includes(label));
      }) ||
      (standard
        ? options.find(candidate =>
            cppStandard(candidate.textContent || "") === standard &&
            /(?:g\+\+|c\+\+)/i.test(candidate.textContent || ""))
        : undefined);
    if (!option) throw new Error(`language "${wanted}" is not available on this submission page`);
    select.value = option.value;
    dispatchChange(select, EventClass);
  }

  function prepareForm(artifact, document, location, EventClass) {
    validateArtifact(artifact);
    if (location.origin !== "https://codeforces.com") {
      throw new Error("submission page is not on Codeforces");
    }
    const contest = location.pathname.match(/^\/contest\/([0-9]+)\/submit\/?$/i);
    const problemset = /^\/problemset\/submit\/?$/i.test(location.pathname);
    if (!contest && !problemset) throw new Error(pageError(document, location));
    if (contest && artifact.target.toUpperCase() !== `${contest[1]}${artifact.index}`.toUpperCase()) {
      throw new Error(`submission target ${artifact.target} does not match this contest`);
    }

    const problemField = contest ? "submittedProblemIndex" : "submittedProblemCode";
    const form = Array.from(document.forms).find(candidate =>
      candidate.querySelector(`[name="${problemField}"]`) &&
      candidate.querySelector('[name="programTypeId"]') &&
      candidate.querySelector('[name="source"]'));
    if (!form) {
      throw new Error(signedOut(document)
        ? "Codeforces is not signed in in Chrome; sign in, then rerun cfx submit"
        : "Codeforces submission form is unavailable; reload Codeforces, then rerun cfx submit");
    }

    const expectedPath = contest ? `/contest/${contest[1]}/submit` : "/problemset/submit";
    const actionUrl = value => new URL(
      value || `${location.pathname}${location.search}`, document.baseURI || location.href
    );
    const validAction = value => {
      const url = actionUrl(value);
      if (
        url.origin !== location.origin || url.username || url.password || url.hash ||
        url.pathname.replace(/\/$/, "") !== expectedPath
      ) {
        throw new Error("Codeforces submission form has an unexpected target");
      }
      return url;
    };
    const action = validAction(form.getAttribute("action"));
    if ((form.method || "get").toLowerCase() !== "post") {
      throw new Error("Codeforces submission form has an unexpected method");
    }
    const submitter = form.querySelector('button[type="submit"], input[type="submit"]');
    const submitAction = submitter?.getAttribute?.("formaction");
    if (submitAction) validAction(submitAction);
    const submitMethod = submitter?.getAttribute?.("formmethod");
    if (submitMethod && submitMethod.toLowerCase() !== "post") {
      throw new Error("Codeforces submit control has an unexpected method");
    }
    const csrf = form.querySelector('[name="csrf_token"]');
    if (!String(csrf?.value || "").trim() && !action.searchParams.get("csrf_token")) {
      throw new Error("Codeforces submission form has no CSRF token; reload and sign in");
    }

    const problem = form.querySelector(`[name="${problemField}"]`);
    const language = form.querySelector('[name="programTypeId"]');
    const source = form.querySelector('[name="source"]');
    if (contest) selectProblem(problem, artifact.index, EventClass);
    else {
      problem.value = artifact.target;
      dispatchChange(problem, EventClass);
    }
    selectLanguage(language, artifact.language, EventClass);
    source.value = artifact.source;
    dispatchChange(source, EventClass);
    return {form, submitter};
  }

  async function waitForChallenge(form, delay, now) {
    const response = () => String(
      form.querySelector('[name="cf-turnstile-response"]')?.value || ""
    ).trim();
    const challenge = form.querySelector('.cf-turnstile, [data-sitekey]');
    if ((!challenge && !form.querySelector('[name="cf-turnstile-response"]')) || response()) return;
    const deadline = now() + 60000;
    while (!response()) {
      if (now() >= deadline) throw new Error("Codeforces verification did not become ready");
      await delay(100);
    }
  }

  function apiRecords(payload) {
    if (
      !payload || payload.status !== "OK" || !Array.isArray(payload.result) ||
      payload.result.length > 100
    ) {
      const comment = String(payload?.comment || "").replace(/\s+/g, " ").trim();
      throw new Error(comment ? `Codeforces API: ${comment}` : "Codeforces API returned invalid data");
    }
    return payload.result;
  }

  async function boundedJson(response, maximumBytes = 256 * 1024) {
    const declared = response.headers?.get?.("content-length");
    if (declared != null && (!/^[0-9]+$/.test(declared) || Number(declared) > maximumBytes)) {
      throw new Error("Codeforces API response is too large");
    }

    const reader = response.body?.getReader?.();
    let text = "";
    if (reader) {
      const decoder = new TextDecoder("utf-8", {fatal: true});
      let bytes = 0;
      while (true) {
        const {done, value} = await reader.read();
        if (done) break;
        bytes += value.byteLength;
        if (bytes > maximumBytes) {
          await reader.cancel();
          throw new Error("Codeforces API response is too large");
        }
        text += decoder.decode(value, {stream: true});
      }
      text += decoder.decode();
    } else {
      if (typeof response.text !== "function") {
        throw new Error("Codeforces API response body is unavailable");
      }
      text = await response.text();
      if (new TextEncoder().encode(text).length > maximumBytes) {
        throw new Error("Codeforces API response is too large");
      }
    }

    try {
      return JSON.parse(text);
    } catch {
      throw new Error("Codeforces API returned invalid JSON");
    }
  }

  function statusUrl(target, handle, origin) {
    const {contest} = targetParts(target);
    const url = new URL("/api/contest.status", origin);
    url.searchParams.set("contestId", contest);
    url.searchParams.set("handle", handle);
    url.searchParams.set("from", "1");
    url.searchParams.set("count", "100");
    return url.href;
  }

  async function recentSubmissions(fetch, target, handle, origin) {
    const url = statusUrl(target, handle, origin);
    const response = await fetch(url, {
      credentials: "omit",
      cache: "no-store",
      redirect: "error",
      referrerPolicy: "no-referrer"
    });
    if (response.redirected || (response.url && response.url !== url)) {
      throw new Error("Codeforces API redirected unexpectedly");
    }
    if (!response.ok) throw new Error(`Codeforces API returned HTTP ${response.status}`);
    return apiRecords(await boundedJson(response));
  }

  function submissionIds(records) {
    return [...new Set(records
      .filter(record => Number.isSafeInteger(record?.id) && record.id > 0)
      .map(record => String(record.id)))];
  }

  function sameHandle(record, handle) {
    return Array.isArray(record?.author?.members) && record.author.members.some(member =>
      typeof member?.handle === "string" && member.handle.toLowerCase() === handle.toLowerCase());
  }

  function identifySubmission(records, pending, nowMillis = Date.now()) {
    const {contest, index} = targetParts(pending.target);
    if (!/^[A-Za-z0-9_.-]+$/.test(pending.handle || "")) {
      throw new Error("pending submission has no valid handle");
    }
    if (!Array.isArray(pending.previousIds) || !Number.isSafeInteger(pending.submittedAtMillis)) {
      throw new Error("pending submission identity is incomplete");
    }
    const previous = new Set(pending.previousIds);
    const earliestSecond = Math.floor(pending.submittedAtMillis / 1000);
    const latestSecond = Math.floor((nowMillis + 5000) / 1000);
    const candidates = new Map();
    for (const record of records) {
      const id = Number.isSafeInteger(record?.id) && record.id > 0 ? String(record.id) : "";
      const created = record?.creationTimeSeconds;
      if (
        id &&
        !previous.has(id) &&
        String(record?.problem?.contestId) === contest &&
        String(record?.problem?.index || "").toUpperCase() === index &&
        sameHandle(record, pending.handle) &&
        Number.isSafeInteger(created) &&
        created >= earliestSecond &&
        created <= latestSecond
      ) {
        candidates.set(id, record);
      }
    }
    if (candidates.size > 1) {
      throw new Error("submission identity is ambiguous; check Codeforces before trying again");
    }
    const id = candidates.keys().next().value;
    return id ? {
      submissionId: id,
      handle: pending.handle,
      submittedAtMillis: pending.submittedAtMillis,
      url: `${originFor(pending)}/contest/${contest}/submission/${id}`
    } : null;
  }

  function originFor(pending) {
    return pending.origin || "https://codeforces.com";
  }

  function formError(document) {
    const elements = document.querySelectorAll(
      ".submit-form .error, form .error, .alert-danger, .error__message"
    );
    for (const element of elements) {
      const text = String(element.textContent || "").replace(/\s+/g, " ").trim();
      if (text) return text;
    }
    return "";
  }

  return {
    apiRecords,
    boundedJson,
    formError,
    identifySubmission,
    loggedInHandle,
    pageError,
    prepareForm,
    publicUrl,
    recentSubmissions,
    signedOut,
    statusUrl,
    submissionIds,
    targetParts,
    validateArtifact,
    waitForChallenge
  };
  })();

  const apiInterval = 2100;
  const challengeDelay = 3000;
  const problemOrigins = [
    "https://codeforces.com",
    "https://m3.codeforces.com",
    "https://mirror.codeforces.com"
  ];

  function parseRequest(rawFragment) {
    const parameters = new URLSearchParams(rawFragment);
    const allowed = new Set(["cfx", "port", "token"]);
    for (const [name] of parameters) {
      if (!allowed.has(name)) throw new Error("unknown connector parameter");
    }
    for (const name of allowed) {
      if (parameters.getAll(name).length !== 1) throw new Error(`invalid connector ${name}`);
    }
    const action = parameters.get("cfx");
    const port = parameters.get("port") || "";
    const token = parameters.get("token") || "";
    if (action !== "fetch" && action !== "submit") throw new Error("unknown connector action");
    if (!/^[0-9]{1,5}$/.test(port) || Number(port) < 1 || Number(port) > 65535) {
      throw new Error("invalid loopback port");
    }
    if (!/^[0-9a-f]{64}$/i.test(token)) throw new Error("invalid connector token");
    return {action, port: Number(port), token: token.toLowerCase()};
  }

  function problemPage(location) {
    return /^\/contest\/[0-9]+\/problem\/[^/]+\/?$/i.test(location.pathname);
  }

  function problemPath(location) {
    return location.pathname.endsWith("/") ? location.pathname.slice(0, -1) : location.pathname;
  }

  function nextProblemUrl(location, position = problemOrigins.indexOf(location.origin)) {
    const next = Math.max(position, problemOrigins.indexOf(location.origin)) + 1;
    if (!problemPage(location) || next <= 0 || next >= problemOrigins.length) {
      return "";
    }
    return new URL(problemPath(location), problemOrigins[next]).href;
  }

  function createConnector(environment) {
    const {chrome, document, location, history, Event: EventClass} = environment;
    const fetch = environment.fetch.bind(environment);
    const now = environment.now || (() => Date.now());
    const delay = environment.delay || (milliseconds =>
      new Promise(resolve => environment.setTimeout(resolve, milliseconds)));
    const domReady = new Promise(resolve => {
      if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", resolve, {once: true});
      } else {
        resolve();
      }
    });

    function messageOf(error) {
      const message = error instanceof Error ? error.message : String(error);
      return message.replace(/\s+/g, " ").trim().slice(0, 1000) || "unknown connector error";
    }

    function cleanPageUrl() {
      const url = new URL(location.href);
      url.hash = "";
      return url.href;
    }

    function showStatus(message, failed = false) {
      let box = document.getElementById("cfx-connector-status");
      if (!box) {
        box = document.createElement("div");
        box.id = "cfx-connector-status";
        Object.assign(box.style, {
          position: "fixed",
          top: "12px",
          right: "12px",
          zIndex: "2147483647",
          maxWidth: "420px",
          padding: "10px 14px",
          borderRadius: "6px",
          color: "white",
          font: "14px/1.4 system-ui, sans-serif",
          boxShadow: "0 2px 12px #0006"
        });
        document.body.appendChild(box);
      }
      box.style.background = failed ? "#a61b1b" : "#246b3c";
      box.textContent = `cfx: ${message}`;
    }

    async function localRequest(request, route, body = "") {
      const result = await chrome.runtime.sendMessage({
        type: "cfx-local-request",
        port: request.port,
        token: request.token,
        route,
        body
      });
      if (!result || result.ok !== true || !Number.isInteger(result.status)) {
        throw new Error(result?.error || "Chrome connector did not return a response");
      }
      return {
        ok: result.status >= 200 && result.status < 300,
        status: result.status,
        json: async () => JSON.parse(result.body)
      };
    }

    async function postLocal(request, route, value) {
      const response = await localRequest(request, route, JSON.stringify(value));
      if (!response.ok) throw new Error(`local workbench returned HTTP ${response.status}`);
    }

    async function reportQuietly(request, route, value) {
      try {
        await postLocal(request, route, value);
        return true;
      } catch {
        return false;
      }
    }

    async function stateRequest(type, kind, action, fields = {}) {
      const result = await chrome.runtime.sendMessage({type, action, ...fields});
      if (!result || result.ok !== true) {
        throw new Error(result?.error || `Chrome connector ${kind}state is unavailable`);
      }
      return result.value;
    }

    function submissionStateRequest(action, operation = "", value) {
      return stateRequest("cfx-submission-state", "", action, {
        ...(operation ? {operation} : {}),
        ...(value ? {value} : {})
      });
    }

    function fetchStateRequest(action, request = {}) {
      return stateRequest("cfx-fetch-state", "fetch ", action, {
        ...(action !== "load" && request.token ? {operation: request.token} : {}),
        ...(action === "save" ? {
          value: {port: request.port, pathname: problemPath(location), position: 0}
        } : {}),
        ...(action === "advance" ? {position: request.position} : {})
      });
    }

    async function removeFetchState(request) {
      try {
        await fetchStateRequest("remove", request);
      } catch {
        // Expiry also removes the operation.
      }
    }

    function browserCheck() {
      const text = `${document.title || ""} ${document.body?.textContent || ""}`;
      return /just a moment|browser is being checked|checking your browser|enable javascript and cookies/i
        .test(text);
    }

    async function handleFetch(request) {
      try {
        let packageValue;
        try {
          packageValue = samples.extractProblem(document, location);
        } catch (error) {
          if (messageOf(error) !== "Codeforces problem statement is unavailable" ||
              !browserCheck()) {
            throw error;
          }
          showStatus("waiting for Codeforces");
          await delay(challengeDelay);
          packageValue = samples.extractProblem(document, location);
        }
        await postLocal(request, "fetch", packageValue);
        await removeFetchState(request);
        showStatus(`${packageValue.tests.length} sample pair(s) sent`);
      } catch (error) {
        let message = messageOf(error);
        const fallback = message === "Codeforces problem statement is unavailable"
          ? nextProblemUrl(location, request.position)
          : "";
        if (fallback) {
          const position = problemOrigins.indexOf(new URL(fallback).origin);
          try {
            await fetchStateRequest("advance", {...request, position});
            showStatus(`trying ${new URL(fallback).host}`);
            location.replace(fallback);
            return;
          } catch (stateError) {
            message = messageOf(stateError);
          }
        }
        await reportQuietly(request, "fetch-error", {message});
        await removeFetchState(request);
        showStatus(message, true);
      }
    }

    async function handleSubmit(request) {
      let saved = false;
      try {
        const artifactResponse = await localRequest(request, "submission");
        if (!artifactResponse.ok) {
          if (artifactResponse.status === 409) {
            showStatus("another connector instance is handling this submission");
            return;
          }
          throw new Error(`local workbench returned HTTP ${artifactResponse.status}`);
        }
        const artifact = submission.validateArtifact(await artifactResponse.json());
        const {form, submitter} = submission.prepareForm(
          artifact, document, location, EventClass
        );
        await submission.waitForChallenge(form, delay, now);

        const handle = submission.loggedInHandle(document, location.origin);
        const records = await submission.recentSubmissions(
          fetch, artifact.target, handle, location.origin
        );
        const lastApiRequestMillis = now();
        const pending = {
          port: request.port,
          target: artifact.target,
          handle,
          previousIds: submission.submissionIds(records),
          submittedAtMillis: now(),
          lastApiRequestMillis
        };
        await submissionStateRequest("save", request.token, pending);
        saved = true;
        showStatus(`submitting ${artifact.target}`);
        if (submitter) form.requestSubmit(submitter);
        else form.requestSubmit();
      } catch (error) {
        if (saved) {
          try {
            await submissionStateRequest("remove", request.token);
          } catch {
            // The original submission error is more useful.
          }
        }
        const result = {ok: false, unknown: false, message: messageOf(error)};
        await reportQuietly(request, "result", result);
        showStatus(result.message, true);
      }
    }

    function submissionResultPage() {
      return (
        /^\/enter\/?$/i.test(location.pathname) ||
        /^\/contest\/[0-9]+\/submit\/?$/i.test(location.pathname) ||
        /^\/problemset\/submit\/?$/i.test(location.pathname) ||
        /^\/problemset\/status\/?$/i.test(location.pathname) ||
        /^\/contest\/[0-9]+\/(?:my|status)\/?$/i.test(location.pathname) ||
        /^\/contest\/[0-9]+\/submission\/[0-9]+\/?$/i.test(location.pathname) ||
        /^\/problemset\/submission\/[0-9]+\/[0-9]+\/?$/i.test(location.pathname) ||
        /^\/submissions\/[^/]+\/?$/i.test(location.pathname)
      );
    }

    async function resumePendingSubmission() {
      if (!submissionResultPage()) return;
      await domReady;

      let stored;
      try {
        stored = await submissionStateRequest("load");
      } catch (error) {
        showStatus(messageOf(error), true);
        return;
      }
      if (!stored) return;

      const pending = {...stored.value, operation: stored.operation, origin: location.origin};
      const request = {action: "submit", port: pending.port, token: pending.operation};
      let result;
      if (/^\/enter\/?$/i.test(location.pathname) || submission.signedOut(document)) {
        result = {ok: false, unknown: false, message: "Codeforces login expired"};
      } else {
        const explicitError = submission.formError(document);
        if (explicitError) result = {ok: false, unknown: false, message: explicitError};
      }

      let lastMessage = "the new submission did not appear in the Codeforces API";
      let stableIdentity = null;
      const candidateIds = new Set();
      while (!result && now() < pending.expiresAtMillis) {
        const wait = pending.lastApiRequestMillis + apiInterval - now();
        if (wait > 0) await delay(Math.min(wait, pending.expiresAtMillis - now()));
        if (now() >= pending.expiresAtMillis) break;
        pending.lastApiRequestMillis = now();
        try {
          const records = await submission.recentSubmissions(
            fetch, pending.target, pending.handle, location.origin
          );
          const identity = submission.identifySubmission(records, pending, now());
          if (identity) {
            candidateIds.add(identity.submissionId);
            if (candidateIds.size > 1) {
              result = {
                ok: false,
                unknown: true,
                message: "submission identity is ambiguous; check Codeforces before trying again"
              };
            } else if (stableIdentity?.submissionId === identity.submissionId) {
              result = {
                ok: true,
                submissionId: identity.submissionId,
                handle: identity.handle,
                submittedAtMillis: identity.submittedAtMillis
              };
              pending.url = identity.url;
            } else {
              stableIdentity = identity;
              showStatus(`confirming submission ${pending.target}`);
            }
          } else {
            stableIdentity = null;
            showStatus(`locating submission ${pending.target}`);
          }
        } catch (error) {
          lastMessage = messageOf(error);
          if (/ambiguous/i.test(lastMessage)) {
            result = {ok: false, unknown: true, message: lastMessage};
          }
        }
      }
      result ||= {
        ok: false,
        unknown: true,
        message: `${lastMessage}; check Codeforces before trying again`
      };

      const reported = await reportQuietly(request, "result", result);
      try {
        await submissionStateRequest("remove", pending.operation);
      } catch {
        // Expiry also removes the operation; reporting is already complete.
      }
      if (result.ok) {
        showStatus(
          reported ? `submitted ${pending.target} as ${result.submissionId}`
            : "submission succeeded, but the local result could not be delivered",
          !reported
        );
        if (pending.url && submission.publicUrl(cleanPageUrl(), location.origin) !== pending.url) {
          location.assign(pending.url);
        }
      } else {
        showStatus(
          result.unknown ? `submission status is unknown: ${result.message}` : result.message,
          true
        );
      }
    }

    async function resumePendingFetch() {
      if (!problemPage(location) || !problemOrigins.includes(location.origin)) return false;
      let pending;
      try {
        pending = await fetchStateRequest("load");
      } catch {
        return false;
      }
      if (!pending) return false;

      const request = {
        action: "fetch",
        port: pending.value.port,
        token: pending.operation,
        position: pending.value.position
      };
      try {
        const response = await localRequest(request, "ready");
        if (!response.ok) throw new Error(`local workbench returned HTTP ${response.status}`);
      } catch {
        await removeFetchState(request);
        return true;
      }
      await domReady;
      await handleFetch(request);
      return true;
    }

    async function dispatchFragment() {
      const rawFragment = location.hash.startsWith("#") ? location.hash.slice(1) : "";
      const parameters = new URLSearchParams(rawFragment);
      if (!parameters.has("cfx")) return;

      const cleanUrl = new URL(location.href);
      cleanUrl.hash = "";
      cleanUrl.searchParams.delete("cfx_reload");
      history.replaceState(history.state, "", `${cleanUrl.pathname}${cleanUrl.search}`);

      let request;
      try {
        request = parseRequest(rawFragment);
        if (request.action === "fetch") {
          await fetchStateRequest("save", request);
          request.position = 0;
        }
        const response = await localRequest(request, "ready");
        if (!response.ok) throw new Error(`local workbench returned HTTP ${response.status}`);
      } catch (error) {
        await domReady;
        const message = messageOf(error);
        if (request?.action === "fetch") {
          await reportQuietly(request, "fetch-error", {message});
          await removeFetchState(request);
        }
        showStatus(message, true);
        return;
      }

      await domReady;
      if (request.action === "fetch") {
        await handleFetch(request);
      } else {
        const error = submission.pageError(document, location);
        if (error) {
          await reportQuietly(request, "result", {ok: false, unknown: false, message: error});
          showStatus(error, true);
        } else {
          await handleSubmit(request);
        }
      }
    }

    function launch() {
      const parameters = new URLSearchParams(
        location.hash.startsWith("#") ? location.hash.slice(1) : ""
      );
      if (parameters.has("cfx")) void dispatchFragment();
      else void (async () => {
        if (!await resumePendingFetch()) await resumePendingSubmission();
      })();
    }

    return {
      dispatchFragment,
      handleSubmit,
      launch,
      resumePendingFetch,
      resumePendingSubmission
    };
  }

  return {createConnector, nextProblemUrl, parseRequest, samples, submission};
});
