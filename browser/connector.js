(() => {
  "use strict";

  const pendingSubmissionKey = "cfx-pending-submission-v1";
  const pendingSubmissionLifetime = 4 * 60 * 1000 + 45 * 1000;
  // Codeforces documents a limit of one API request every two seconds.
  const verdictPollInterval = 2100;
  const pendingVerdicts = new Set(["", "NULL", "SUBMITTED", "TESTING"]);

  const domReady = new Promise(resolve => {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", resolve, {once: true});
    } else {
      resolve();
    }
  });

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

  function messageOf(error) {
    const message = error instanceof Error ? error.message : String(error);
    return message.replace(/\s+/g, " ").trim().slice(0, 1000) || "unknown connector error";
  }

  function parseRequest(rawFragment) {
    const parameters = new URLSearchParams(rawFragment);
    const action = parameters.get("cfx");
    const port = parameters.get("port") || "";
    const token = parameters.get("token") || "";
    if (action !== "fetch" && action !== "submit") {
      throw new Error("unknown connector action");
    }
    if (!/^[0-9]{1,5}$/.test(port) || Number(port) < 1 || Number(port) > 65535) {
      throw new Error("invalid loopback port");
    }
    if (!/^[0-9a-fA-F]{64}$/.test(token)) {
      throw new Error("invalid connector token");
    }
    return {action, port: Number(port), token};
  }

  function clearPendingSubmission() {
    try {
      sessionStorage.removeItem(pendingSubmissionKey);
    } catch {
      // A failed cleanup must not hide the submission result.
    }
  }

  function savePendingSubmission(request, artifact, knownSubmissionIds) {
    sessionStorage.setItem(
      pendingSubmissionKey,
      JSON.stringify({
        port: request.port,
        token: request.token,
        target: artifact.target,
        knownSubmissionIds: Array.from(knownSubmissionIds),
        createdAt: Date.now(),
        submissionId: "",
        submissionUrl: "",
        authorHandle: ""
      })
    );
  }

  function pendingSubmission() {
    let raw;
    try {
      raw = sessionStorage.getItem(pendingSubmissionKey);
    } catch {
      return null;
    }
    if (!raw) {
      return null;
    }
    try {
      const value = JSON.parse(raw);
      if (
        !value ||
        !Number.isInteger(value.port) ||
        value.port < 1 ||
        value.port > 65535 ||
        typeof value.token !== "string" ||
        !/^[0-9a-f]{64}$/i.test(value.token) ||
        typeof value.target !== "string" ||
        !/^[0-9]+[A-Za-z][A-Za-z0-9]*$/.test(value.target) ||
        !Array.isArray(value.knownSubmissionIds) ||
        !value.knownSubmissionIds.every(id => typeof id === "string" && /^[0-9]+$/.test(id)) ||
        !Number.isFinite(value.createdAt) ||
        value.createdAt > Date.now() + 5000 ||
        Date.now() - value.createdAt > pendingSubmissionLifetime ||
        (value.submissionId !== undefined &&
          (typeof value.submissionId !== "string" ||
            (value.submissionId && !/^[0-9]+$/.test(value.submissionId)))) ||
        (value.submissionUrl !== undefined && typeof value.submissionUrl !== "string") ||
        (value.authorHandle !== undefined &&
          (typeof value.authorHandle !== "string" ||
            (value.authorHandle && !/^[A-Za-z0-9_.-]+$/.test(value.authorHandle))))
      ) {
        throw new Error("stale pending submission");
      }
      const submissionUrl = value.submissionUrl
        ? publicCodeforcesUrl(value.submissionUrl)
        : "";
      if (submissionUrl) {
        const pathname = new URL(submissionUrl).pathname;
        const match =
          pathname.match(/^\/contest\/([0-9]+)\/submission\/([0-9]+)\/?$/i) ||
          pathname.match(/^\/problemset\/submission\/([0-9]+)\/([0-9]+)\/?$/i);
        const contest = value.target.match(/^([0-9]+)/)?.[1];
        if (!match || match[1] !== contest || match[2] !== value.submissionId) {
          throw new Error("pending submission identity does not match its URL");
        }
      }
      return {
        request: {action: "submit", port: value.port, token: value.token},
        target: value.target,
        knownSubmissionIds: new Set(value.knownSubmissionIds),
        createdAt: value.createdAt,
        submissionId: value.submissionId || "",
        submissionUrl,
        authorHandle: value.authorHandle || ""
      };
    } catch {
      clearPendingSubmission();
      return null;
    }
  }

  function updatePendingSubmission(pending) {
    sessionStorage.setItem(
      pendingSubmissionKey,
      JSON.stringify({
        port: pending.request.port,
        token: pending.request.token,
        target: pending.target,
        knownSubmissionIds: Array.from(pending.knownSubmissionIds),
        createdAt: pending.createdAt,
        submissionId: pending.submissionId || "",
        submissionUrl: pending.submissionUrl || "",
        authorHandle: pending.authorHandle || ""
      })
    );
  }

  function delay(milliseconds) {
    return new Promise(resolve => setTimeout(resolve, milliseconds));
  }

  async function localRequest(request, route, options = {}) {
    const method = options.method || "GET";
    const result = await chrome.runtime.sendMessage({
      type: "cfx-local-request",
      port: request.port,
      token: request.token,
      route,
      method,
      body: options.body || ""
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
    const response = await localRequest(request, route, {
      method: "POST",
      headers: {"Content-Type": "text/plain;charset=UTF-8"},
      body: JSON.stringify(value)
    });
    if (!response.ok) {
      throw new Error(`local workbench returned HTTP ${response.status}`);
    }
  }

  async function reportQuietly(request, route, value) {
    try {
      await postLocal(request, route, value);
      return true;
    } catch {
      return false;
    }
  }

  function cleanPageUrl() {
    const url = new URL(location.href);
    url.hash = "";
    return url.href;
  }

  function problemIndex() {
    const match =
      location.pathname.match(/^\/contest\/[0-9]+\/problem\/([^/]+)/i) ||
      location.pathname.match(/^\/problemset\/problem\/[0-9]+\/([^/]+)/i);
    return match ? decodeURIComponent(match[1]).toUpperCase() : "";
  }

  function parseTimeLimit(text) {
    const match = text
      .replace(",", ".")
      .match(/([0-9]+(?:\.[0-9]+)?)\s*(milliseconds?|ms|seconds?|s)\b/i);
    if (!match) {
      throw new Error("cannot read the time limit");
    }
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
    if (!match) {
      throw new Error("cannot read the memory limit");
    }
    const unit = match[2].toLowerCase();
    const factor = unit.startsWith("g") ? 1024 : unit.startsWith("k") ? 1 / 1024 : 1;
    const megabytes = Number(match[1]) * factor;
    if (!Number.isFinite(megabytes) || megabytes <= 0 || megabytes > 2147483647) {
      throw new Error("invalid memory limit");
    }
    return Math.round(megabytes);
  }

  function renderedSample(pre) {
    const text =
      typeof pre.innerText === "string"
        ? pre.innerText
        : (() => {
            const copy = pre.cloneNode(true);
            for (const lineBreak of copy.querySelectorAll("br")) {
              lineBreak.replaceWith("\n");
            }
            return copy.textContent || "";
          })();
    return text.replace(/\r\n?/g, "\n");
  }

  function extractProblem() {
    const statement = document.querySelector(".problem-statement");
    if (!statement) {
      throw new Error("Codeforces problem statement is unavailable");
    }
    const titleElement = statement.querySelector(".header .title");
    const timeElement = statement.querySelector(".header .time-limit");
    const memoryElement = statement.querySelector(".header .memory-limit");
    if (!titleElement || !timeElement || !memoryElement) {
      throw new Error("Codeforces problem metadata is incomplete");
    }

    let name = (titleElement.textContent || "").replace(/\s+/g, " ").trim();
    const index = problemIndex();
    if (index && name.toUpperCase().startsWith(`${index}.`)) {
      name = name.slice(index.length + 1).trim();
    }
    if (!name) {
      throw new Error("Codeforces problem has no title");
    }

    const inputs = Array.from(statement.querySelectorAll(".sample-test .input pre"));
    const outputs = Array.from(statement.querySelectorAll(".sample-test .output pre"));
    if (inputs.length === 0 || inputs.length !== outputs.length) {
      throw new Error("Codeforces sample input/output pairs are incomplete");
    }

    return {
      name,
      url: cleanPageUrl(),
      timeLimit: parseTimeLimit(timeElement.textContent || ""),
      memoryLimit: parseMemoryLimit(memoryElement.textContent || ""),
      tests: inputs.map((input, position) => ({
        input: renderedSample(input),
        output: renderedSample(outputs[position])
      }))
    };
  }

  function dispatchChange(element) {
    element.dispatchEvent(new Event("input", {bubbles: true}));
    element.dispatchEvent(new Event("change", {bubbles: true}));
  }

  function selectProblem(select, index) {
    const option = Array.from(select.options).find(
      candidate => candidate.value.toUpperCase() === index.toUpperCase()
    );
    if (!option) {
      throw new Error(`problem ${index} is not available on this submission page`);
    }
    select.value = option.value;
    dispatchChange(select);
  }

  function normalizedLanguage(value) {
    return value.toLowerCase().replace(/\+\+/g, "pp").replace(/[^a-z0-9]+/g, "");
  }

  function cppStandard(value) {
    const match = value.toLowerCase().match(/(?:gnu\s*)?(?:g\+\+|c\+\+|cpp)\D*([0-9]{2})/);
    return match ? match[1] : "";
  }

  function selectLanguage(select, requested) {
    const wanted = String(requested);
    const normalized = normalizedLanguage(wanted);
    const standard = cppStandard(wanted);
    const options = Array.from(select.options).filter(option => !option.disabled);
    let option = options.find(candidate => candidate.value === wanted);
    option ||= options.find(candidate => normalizedLanguage(candidate.textContent || "") === normalized);
    option ||= options.find(candidate => {
      const label = normalizedLanguage(candidate.textContent || "");
      return normalized.length > 2 && (label.includes(normalized) || normalized.includes(label));
    });
    option ||= standard
      ? options.find(
          candidate =>
            cppStandard(candidate.textContent || "") === standard &&
            /(?:g\+\+|c\+\+)/i.test(candidate.textContent || "")
        )
      : undefined;
    if (!option) {
      throw new Error(`language "${wanted}" is not available on this submission page`);
    }
    select.value = option.value;
    dispatchChange(select);
  }

  function validateArtifact(value) {
    if (!value || typeof value !== "object") {
      throw new Error("local workbench returned an invalid submission");
    }
    for (const field of ["target", "index", "language", "source"]) {
      if (typeof value[field] !== "string") {
        throw new Error(`local submission has no ${field}`);
      }
    }
    if (!/^[A-Za-z][A-Za-z0-9]*$/.test(value.index) || value.source.length === 0) {
      throw new Error("local submission fields are invalid");
    }
    return value;
  }

  function publicCodeforcesUrl(value) {
    const url = new URL(value, location.origin);
    if (url.origin !== location.origin) {
      throw new Error("Codeforces redirected outside its own origin");
    }
    url.search = "";
    url.hash = "";
    return url.href;
  }

  function signedOut() {
    return Boolean(
      document.querySelector('a[href^="/enter"], form[action*="/enter"]')
    );
  }

  function submissionPageError() {
    if (signedOut()) {
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

  function submissionForm(artifact) {
    const contest = location.pathname.match(/^\/contest\/([0-9]+)\/submit\/?$/i);
    const problemset = /^\/problemset\/submit\/?$/i.test(location.pathname);
    if (!contest && !problemset) {
      throw new Error(submissionPageError());
    }
    if (
      contest &&
      artifact.target.toUpperCase() !== `${contest[1]}${artifact.index}`.toUpperCase()
    ) {
      throw new Error(`submission target ${artifact.target} does not match this contest`);
    }

    const problemField = contest ? "submittedProblemIndex" : "submittedProblemCode";
    const form = Array.from(document.forms).find(
      candidate =>
        candidate.querySelector(`[name="${problemField}"]`) &&
        candidate.querySelector('[name="programTypeId"]') &&
        candidate.querySelector('[name="source"]')
    );
    if (!form) {
      throw new Error(
        signedOut()
          ? "Codeforces is not signed in in Chrome; sign in, then rerun cfx submit"
          : "Codeforces submission form is unavailable; reload Codeforces, then rerun cfx submit"
      );
    }
    const problem = form.querySelector(`[name="${problemField}"]`);
    const language = form.querySelector('[name="programTypeId"]');
    const source = form.querySelector('[name="source"]');
    if (contest) {
      selectProblem(problem, artifact.index);
    } else {
      problem.value = artifact.target;
      dispatchChange(problem);
    }
    selectLanguage(language, artifact.language);
    source.value = artifact.source;
    dispatchChange(source);

    const action = new URL(
      form.getAttribute("action") || `${location.pathname}${location.search}`,
      location.origin
    );
    action.hash = "";
    if (action.origin !== location.origin || (form.method || "get").toLowerCase() !== "post") {
      throw new Error("Codeforces submission form has an unexpected target");
    }
    const submitter = form.querySelector('button[type="submit"], input[type="submit"]');
    const csrf = form.querySelector('[name="csrf_token"]');
    if (!String(csrf?.value || "").trim() && !action.searchParams.get("csrf_token")) {
      throw new Error("Codeforces submission form has no CSRF token; reload and sign in");
    }
    return {form, submitter};
  }

  async function waitForSubmissionChallenge(form) {
    const responseValue = () =>
      String(form.querySelector('[name="cf-turnstile-response"]')?.value || "").trim();
    const challenge = form.querySelector('.cf-turnstile, [data-sitekey]');
    if ((!challenge && !form.querySelector('[name="cf-turnstile-response"]')) || responseValue()) {
      return;
    }
    await new Promise((resolve, reject) => {
      const started = Date.now();
      const interval = setInterval(() => {
        if (responseValue()) {
          clearInterval(interval);
          resolve();
        } else if (Date.now() - started >= 60000) {
          clearInterval(interval);
          reject(new Error("Codeforces verification did not become ready"));
        }
      }, 100);
    });
  }

  function formError(documentCopy) {
    const selectors = [
      ".submit-form .error",
      "form .error",
      ".alert-danger",
      ".error__message"
    ];
    for (const element of documentCopy.querySelectorAll(selectors.join(","))) {
      const text = (element.textContent || "").replace(/\s+/g, " ").trim();
      if (text) {
        return text;
      }
    }
    return "";
  }

  function submissionId(link) {
    const match = new URL(link.href, location.origin).pathname.match(
      /\/submission\/(?:[0-9]+\/)?([0-9]+)\/?$/
    );
    return match ? match[1] : "";
  }

  function submissionIds(root) {
    return new Set(
      Array.from(root.querySelectorAll('a[href*="/submission/"]'))
        .map(submissionId)
        .filter(Boolean)
    );
  }

  function rowProblemTarget(row) {
    const link = row.querySelector(
      'a[href*="/problem/"], a[href*="/problemset/problem/"]'
    );
    if (!link) {
      return "";
    }
    const pathname = new URL(link.href, location.origin).pathname;
    const match =
      pathname.match(/^\/contest\/([0-9]+)\/problem\/([^/]+)\/?$/i) ||
      pathname.match(/^\/problemset\/problem\/([0-9]+)\/([^/]+)\/?$/i);
    return match ? `${match[1]}${decodeURIComponent(match[2])}`.toUpperCase() : "";
  }

  function rowAuthorHandle(container) {
    const link = container.querySelector(
      'a[href^="/profile/"], a[href*="codeforces.com/profile/"]'
    );
    if (!link) return "";
    const match = new URL(link.href, location.origin).pathname.match(/^\/profile\/([^/]+)\/?$/i);
    if (!match) return "";
    try {
      return decodeURIComponent(match[1]);
    } catch {
      return "";
    }
  }

  function compactText(value) {
    return String(value || "").replace(/\s+/g, " ").trim();
  }

  function parseConsumedTime(value) {
    const match = compactText(value)
      .replace(",", ".")
      .match(/^([0-9]+(?:\.[0-9]+)?)\s*(milliseconds?|ms|seconds?|s)$/i);
    if (!match) return null;
    const milliseconds = Number(match[1]) * (/^(?:milliseconds?|ms)$/i.test(match[2]) ? 1 : 1000);
    return Number.isSafeInteger(Math.round(milliseconds)) && milliseconds >= 0
      ? Math.round(milliseconds)
      : null;
  }

  function parseConsumedMemory(value) {
    const match = compactText(value)
      .replace(",", ".")
      .match(/^([0-9]+(?:\.[0-9]+)?)\s*(bytes?|b|kib|kb|mib|mb|gib|gb)$/i);
    if (!match) return null;
    const unit = match[2].toLowerCase();
    const factor = unit === "b" || unit.startsWith("byte")
      ? 1
      : unit.startsWith("k")
        ? 1024
        : unit.startsWith("m")
          ? 1024 ** 2
          : 1024 ** 3;
    const bytes = Math.round(Number(match[1]) * factor);
    return Number.isSafeInteger(bytes) && bytes >= 0 ? bytes : null;
  }

  function verdictText(verdict) {
    const names = {
      OK: "Accepted",
      PARTIAL: "Partial",
      COMPILATION_ERROR: "Compilation Error",
      RUNTIME_ERROR: "Runtime Error",
      WRONG_ANSWER: "Wrong Answer",
      PRESENTATION_ERROR: "Presentation Error",
      TIME_LIMIT_EXCEEDED: "Time Limit Exceeded",
      MEMORY_LIMIT_EXCEEDED: "Memory Limit Exceeded",
      IDLENESS_LIMIT_EXCEEDED: "Idleness Limit Exceeded",
      SECURITY_VIOLATED: "Security Violation",
      CRASHED: "Crashed",
      INPUT_PREPARATION_CRASHED: "Input Preparation Crashed",
      CHALLENGED: "Challenged",
      SKIPPED: "Skipped",
      REJECTED: "Rejected",
      FAILED: "Failed"
    };
    return names[verdict] || verdict
      .toLowerCase()
      .split("_")
      .map(word => word ? word[0].toUpperCase() + word.slice(1) : word)
      .join(" ");
  }

  function apiInteger(record, name) {
    const value = record?.[name];
    if (!Number.isSafeInteger(value) || value < 0) {
      throw new Error(`Codeforces API returned an invalid ${name}`);
    }
    return value;
  }

  function submissionApiOutcome(payload, pending, now = Date.now()) {
    if (!payload || payload.status !== "OK" || !Array.isArray(payload.result)) {
      const comment = compactText(payload?.comment);
      throw new Error(
        comment ? `Codeforces API: ${comment}` : "Codeforces API returned invalid data"
      );
    }
    const record = payload.result.find(value =>
      Number.isSafeInteger(value?.id) && String(value.id) === pending.submissionId
    );
    if (!record) {
      return {
        pending: true,
        submissionId: pending.submissionId,
        url: pending.submissionUrl,
        verdict: "",
        verdictText: "",
        message: "waiting for the submission to appear in the Codeforces API"
      };
    }

    const verdict = compactText(record.verdict).toUpperCase();
    if (pendingVerdicts.has(verdict)) {
      return {
        pending: true,
        submissionId: pending.submissionId,
        url: pending.submissionUrl,
        verdict,
        verdictText: verdict === "TESTING" ? "Judging" : "Queued",
        message: "Codeforces is still judging"
      };
    }
    if (!/^[A-Z_]+$/.test(verdict)) {
      throw new Error("Codeforces API returned an invalid verdict");
    }
    const testset = compactText(record.testset).toUpperCase();

    return {
      ok: true,
      unknown: false,
      url: pending.submissionUrl,
      submissionId: pending.submissionId,
      verdict,
      verdictText: verdict === "OK" && testset === "PRETESTS"
        ? "Accepted (pretests)"
        : verdictText(verdict),
      passedTestCount: apiInteger(record, "passedTestCount"),
      timeConsumedMillis: apiInteger(record, "timeConsumedMillis"),
      memoryConsumedBytes: apiInteger(record, "memoryConsumedBytes"),
      judgingWaitMillis: Math.max(0, Math.round(now - pending.createdAt)),
      message: "judging complete"
    };
  }

  async function fetchSubmissionOutcome(pending) {
    const contest = pending.target.match(/^([0-9]+)[A-Za-z]/)?.[1];
    if (!contest || !pending.authorHandle || !pending.submissionId || !pending.submissionUrl) {
      throw new Error("submission identity is incomplete");
    }
    const url = new URL("/api/contest.status", location.origin);
    url.searchParams.set("contestId", contest);
    url.searchParams.set("handle", pending.authorHandle);
    url.searchParams.set("from", "1");
    url.searchParams.set("count", "50");
    const response = await fetch(url.href, {
      credentials: "omit",
      cache: "no-store",
      redirect: "follow",
      referrerPolicy: "no-referrer"
    });
    if (!response.ok) {
      throw new Error(`Codeforces API returned HTTP ${response.status}`);
    }
    return submissionApiOutcome(await response.json(), pending);
  }

  async function fetchDisplayedSubmissionMetrics(pending) {
    const response = await fetch(pending.submissionUrl, {
      credentials: "include",
      cache: "no-store",
      redirect: "follow",
      referrerPolicy: "strict-origin-when-cross-origin"
    });
    if (!response.ok) return null;
    const pageResult = submissionOutcome(
      response,
      await response.text(),
      pending.knownSubmissionIds,
      pending.target,
      pending.submissionId
    );
    return pageResult.ok && Number.isSafeInteger(pageResult.timeConsumedMillis) &&
        Number.isSafeInteger(pageResult.memoryConsumedBytes)
      ? pageResult
      : null;
  }

  function submissionOutcome(
    response,
    html,
    knownSubmissionIds,
    target = "",
    expectedSubmissionId = ""
  ) {
    const documentCopy = new DOMParser().parseFromString(html, "text/html");
    const responseUrl = publicCodeforcesUrl(response.url || cleanPageUrl());
    if (documentCopy.querySelector('form[action*="/enter"]') || /\/enter\/?$/i.test(responseUrl)) {
      return {ok: false, url: responseUrl, verdict: "", message: "Codeforces login expired"};
    }

    const error = formError(documentCopy);
    const stayedOnForm = /\/submit\/?$/i.test(new URL(responseUrl).pathname);
    if (error) {
      return {
        ok: false,
        url: responseUrl,
        verdict: "",
        message: error
      };
    }
    if (!response.ok || stayedOnForm) {
      return {
        ok: false,
        unknown: true,
        url: responseUrl,
        verdict: "",
        message: !response.ok
          ? `Codeforces returned HTTP ${response.status}; check Codeforces before trying again`
          : "Codeforces stayed on the submission form without an error; check before trying again"
      };
    }

    const rows = Array.from(
      documentCopy.querySelectorAll("tr[data-submission-id], table.status-frame-datatable tr")
    );
    const row = rows.find(candidate => {
      const candidateLink = candidate.querySelector('a[href*="/submission/"]');
      const id = candidateLink ? submissionId(candidateLink) : "";
      const candidateTarget = rowProblemTarget(candidate);
      return (
        id &&
        (expectedSubmissionId ? id === expectedSubmissionId : !knownSubmissionIds.has(id)) &&
        (!target || candidateTarget === target.toUpperCase())
      );
    });
    const responseSubmissionId = submissionId({href: responseUrl});
    const directTarget = responseSubmissionId ? rowProblemTarget(documentCopy) : "";
    const directId =
      responseSubmissionId &&
      (expectedSubmissionId
        ? responseSubmissionId === expectedSubmissionId
        : !knownSubmissionIds.has(responseSubmissionId)) &&
      (!target || directTarget === target.toUpperCase())
        ? responseSubmissionId
        : "";
    const link = row?.querySelector('a[href*="/submission/"]');
    const id = link ? submissionId(link) : directId;
    const url = link ? publicCodeforcesUrl(link.href) : responseUrl;
    const container = row || (directId ? documentCopy : null);
    if (!container || !id) {
      return {
        pending: true,
        url: responseUrl,
        submissionId: expectedSubmissionId,
        verdict: "",
        verdictText: "",
        message: "waiting for the new submission to appear"
      };
    }

    const authorHandle = rowAuthorHandle(container);

    const verdictElement = container.querySelector(
      ".submissionVerdictWrapper[submissionverdict], [submissionverdict]"
    );
    const verdict = compactText(verdictElement?.getAttribute("submissionverdict")).toUpperCase();
    const verdictText = compactText(verdictElement?.textContent) || verdict;
    if (pendingVerdicts.has(verdict)) {
      return {
        pending: true,
        url,
        submissionId: id,
        authorHandle,
        verdict,
        verdictText,
        message: verdictText || "judging"
      };
    }

    const timeConsumedMillis = parseConsumedTime(
      container.querySelector(".time-consumed-cell")?.textContent
    );
    const memoryConsumedBytes = parseConsumedMemory(
      container.querySelector(".memory-consumed-cell")?.textContent
    );
    if (timeConsumedMillis === null || memoryConsumedBytes === null) {
      return {
        pending: true,
        url,
        submissionId: id,
        authorHandle,
        verdict,
        verdictText,
        message: "waiting for final resource measurements"
      };
    }
    return {
      ok: true,
      unknown: false,
      url,
      submissionId: id,
      authorHandle,
      verdict,
      verdictText,
      timeConsumedMillis,
      memoryConsumedBytes,
      message: "judging complete"
    };
  }

  async function handleFetch(request) {
    try {
      const packageValue = extractProblem();
      await postLocal(request, "fetch", packageValue);
      showStatus(`${packageValue.tests.length} sample pair(s) sent`);
    } catch (error) {
      const message = messageOf(error);
      await reportQuietly(request, "fetch-error", {message});
      showStatus(message, true);
    }
  }

  async function handleSubmit(request) {
    let result;
    try {
      const artifactResponse = await localRequest(request, "submission");
      if (!artifactResponse.ok) {
        if (artifactResponse.status === 409) {
          showStatus("another connector instance is handling this submission");
          return;
        }
        throw new Error(`local workbench returned HTTP ${artifactResponse.status}`);
      }
      const artifact = validateArtifact(await artifactResponse.json());
      const {form, submitter} = submissionForm(artifact);
      await waitForSubmissionChallenge(form);
      const knownSubmissionIds = submissionIds(document);
      savePendingSubmission(request, artifact, knownSubmissionIds);
      showStatus(`submitting ${artifact.target}`);
      try {
        if (submitter) {
          form.requestSubmit(submitter);
        } else {
          form.requestSubmit();
        }
      } catch (error) {
        clearPendingSubmission();
        throw error;
      }
      return;
    } catch (error) {
      result = {
        ok: false,
        unknown: false,
        url: publicCodeforcesUrl(cleanPageUrl()),
        verdict: "",
        message: messageOf(error)
      };
    }

    const reported = await reportQuietly(request, "result", result);
    if (result.ok) {
      if (!reported) {
        showStatus("submission succeeded, but the local result could not be delivered", true);
      }
      location.assign(result.url);
    } else {
      showStatus(
        result.unknown ? `submission status is unknown: ${result.message}` : result.message,
        true
      );
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
    const pending = pendingSubmission();
    if (!pending || !submissionResultPage()) {
      return;
    }

    await domReady;
    let result;
    let lastMessage = "Codeforces did not finish judging before the local wait limit";
    const deadline = pending.createdAt + pendingSubmissionLifetime;
    let pageResponse = {ok: true, status: 200, url: cleanPageUrl()};
    let pageHtml = document.documentElement?.outerHTML || "";
    while (Date.now() < deadline) {
      try {
        if (!pending.submissionId || !pending.submissionUrl || !pending.authorHandle) {
          const pageResult = submissionOutcome(
            pageResponse,
            pageHtml,
            pending.knownSubmissionIds,
            pending.target,
            pending.submissionId
          );
          if (pageResult.ok === false && !pageResult.pending) {
            result = pageResult;
            break;
          }
          lastMessage = pageResult.message || lastMessage;
          let identityChanged = false;
          for (const [field, value] of [
            ["submissionId", pageResult.submissionId],
            ["submissionUrl", pageResult.url],
            ["authorHandle", pageResult.authorHandle]
          ]) {
            if (value && value !== pending[field]) {
              pending[field] = value;
              identityChanged = true;
            }
          }
          if (identityChanged) updatePendingSubmission(pending);
        }

        if (pending.submissionId && pending.submissionUrl && pending.authorHandle) {
          result = await fetchSubmissionOutcome(pending);
          if (!result.pending) {
            try {
              const displayed = await fetchDisplayedSubmissionMetrics(pending);
              if (displayed) {
                result.timeConsumedMillis = displayed.timeConsumedMillis;
                result.memoryConsumedBytes = displayed.memoryConsumedBytes;
              }
            } catch {
              // The official API result remains complete if the signed detail page cannot reload.
            }
            result.judgingWaitMillis = Math.max(0, Date.now() - pending.createdAt);
            break;
          }
          lastMessage = result.message || lastMessage;
          showStatus(`${result.verdictText || "Judging"} ${pending.target}`);
        } else {
          result = {pending: true};
          showStatus(`Locating submission ${pending.target}`);
        }
      } catch (error) {
        lastMessage = messageOf(error);
        result = {pending: true};
      }

      await delay(verdictPollInterval);
      if (!pending.submissionId || !pending.submissionUrl || !pending.authorHandle) {
        try {
          pageResponse = await fetch(cleanPageUrl(), {
            credentials: "include",
            cache: "no-store",
            redirect: "follow",
            referrerPolicy: "strict-origin-when-cross-origin"
          });
          pageHtml = await pageResponse.text();
        } catch (error) {
          lastMessage = messageOf(error);
        }
      }
    }
    if (!result || result.pending) {
      result = {
        ok: false,
        unknown: true,
        url: pending.submissionUrl || publicCodeforcesUrl(cleanPageUrl()),
        submissionId: pending.submissionId,
        verdict: "",
        judgingWaitMillis: Math.max(0, Date.now() - pending.createdAt),
        message: `${lastMessage}; check Codeforces before trying again`
      };
    }

    let reported = false;
    try {
      reported = await reportQuietly(pending.request, "result", result);
    } finally {
      clearPendingSubmission();
    }
    if (result.ok) {
      if (!reported) {
        showStatus("submission succeeded, but the local result could not be delivered", true);
      } else {
        showStatus(
          `${result.verdictText || result.verdict} — ${result.timeConsumedMillis} ms, ` +
            `${Math.round(result.memoryConsumedBytes / 1024)} KB`,
          result.verdict !== "OK"
        );
      }
      if (publicCodeforcesUrl(cleanPageUrl()) !== result.url) {
        location.assign(result.url);
      }
    } else {
      showStatus(
        result.unknown ? `submission status is unknown: ${result.message}` : result.message,
        true
      );
    }
  }

  async function dispatchFragment() {
    const rawFragment = location.hash.startsWith("#") ? location.hash.slice(1) : "";
    const parameters = new URLSearchParams(rawFragment);
    if (!parameters.has("cfx")) {
      return;
    }

    const cleanUrl = new URL(location.href);
    cleanUrl.hash = "";
    cleanUrl.searchParams.delete("cfx_reload");
    history.replaceState(history.state, "", `${cleanUrl.pathname}${cleanUrl.search}`);

    let request;
    try {
      request = parseRequest(rawFragment);
      const response = await localRequest(request, "ready");
      if (!response.ok) {
        throw new Error(`local workbench returned HTTP ${response.status}`);
      }
    } catch (error) {
      await domReady;
      showStatus(messageOf(error), true);
      return;
    }

    await domReady;
    if (request.action === "fetch") {
      await handleFetch(request);
    } else {
      const error = submissionPageError();
      if (error) {
        await reportQuietly(request, "result", {
          ok: false,
          unknown: false,
          url: publicCodeforcesUrl(cleanPageUrl()),
          verdict: "",
          message: error
        });
        showStatus(error, true);
        return;
      }
      await handleSubmit(request);
    }
  }

  const launchParameters = new URLSearchParams(
    location.hash.startsWith("#") ? location.hash.slice(1) : ""
  );
  if (launchParameters.has("cfx")) {
    void dispatchFragment();
  } else {
    void resumePendingSubmission();
  }
})();
