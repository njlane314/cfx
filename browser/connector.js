(() => {
  "use strict";

  const domReady = new Promise(resolve => {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", resolve, {once: true});
    } else {
      resolve();
    }
  });

  function showStatus(message, failed = false) {
    let box = document.getElementById("cfprobs-connector-status");
    if (!box) {
      box = document.createElement("div");
      box.id = "cfprobs-connector-status";
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
    box.textContent = `cf-probs: ${message}`;
  }

  function messageOf(error) {
    const message = error instanceof Error ? error.message : String(error);
    return message.replace(/\s+/g, " ").trim().slice(0, 1000) || "unknown connector error";
  }

  function parseRequest(rawFragment) {
    const parameters = new URLSearchParams(rawFragment);
    const action = parameters.get("cfprobs");
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

  async function localRequest(request, route, options = {}) {
    const method = options.method || "GET";
    const result = await chrome.runtime.sendMessage({
      type: "cfprobs-local-request",
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
      return "Codeforces is not signed in in Chrome; sign in, then rerun probs submit";
    }
    if (
      !/^\/contest\/[0-9]+\/submit\/?$/i.test(location.pathname) &&
      !/^\/problemset\/submit\/?$/i.test(location.pathname)
    ) {
      return "Codeforces redirected away from its submission page; reload Codeforces, then rerun probs submit";
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
          ? "Codeforces is not signed in in Chrome; sign in, then rerun probs submit"
          : "Codeforces submission form is unavailable; reload Codeforces, then rerun probs submit"
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
    const data = new FormData(form);
    const submitter = form.querySelector('button[type="submit"], input[type="submit"]');
    if (submitter?.name && !data.has(submitter.name)) {
      data.append(submitter.name, submitter.value);
    }
    if (!data.get("csrf_token") && !action.searchParams.get("csrf_token")) {
      throw new Error("Codeforces submission form has no CSRF token; reload and sign in");
    }
    return {action, data};
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
    const match = new URL(link.href, location.origin).pathname.match(/\/submission\/([0-9]+)\/?$/);
    return match ? match[1] : "";
  }

  function submissionIds(root) {
    return new Set(
      Array.from(root.querySelectorAll('a[href*="/submission/"]'))
        .map(submissionId)
        .filter(Boolean)
    );
  }

  function submissionOutcome(response, html, knownSubmissionIds) {
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

    if (/\/submission\/[0-9]+\/?$/i.test(new URL(responseUrl).pathname)) {
      return {
        ok: true,
        url: responseUrl,
        verdict: "submitted",
        message: "submission created"
      };
    }

    const rows = Array.from(
      documentCopy.querySelectorAll("tr[data-submission-id], table.status-frame-datatable tr")
    );
    const row = rows.find(candidate => {
      const candidateLink = candidate.querySelector('a[href*="/submission/"]');
      const id = candidateLink ? submissionId(candidateLink) : "";
      return id && !knownSubmissionIds.has(id);
    });
    const link = row?.querySelector('a[href*="/submission/"]');
    if (link) {
      const verdictElement = row.querySelector(
        "[submissionverdict], .submissionVerdictWrapper, [class*='verdict']"
      );
      const verdict =
        (verdictElement?.textContent || verdictElement?.getAttribute("submissionverdict") || "")
          .replace(/\s+/g, " ")
          .trim() || "submitted";
      return {
        ok: true,
        url: publicCodeforcesUrl(link.href),
        verdict,
        message: "submission created"
      };
    }

    return {
      ok: false,
      unknown: true,
      url: responseUrl,
      verdict: "",
      message: "Codeforces returned an unrecognized response after submission"
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
    let submissionStarted = false;
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
      const {action, data} = submissionForm(artifact);
      const knownSubmissionIds = submissionIds(document);
      showStatus(`submitting ${artifact.target}`);
      submissionStarted = true;
      const response = await fetch(action, {
        method: "POST",
        body: data,
        credentials: "include",
        cache: "no-store",
        redirect: "follow",
        referrer: `${location.origin}${location.pathname}`,
        referrerPolicy: "strict-origin-when-cross-origin"
      });
      result = submissionOutcome(response, await response.text(), knownSubmissionIds);
    } catch (error) {
      const message = messageOf(error);
      result = {
        ok: false,
        unknown: submissionStarted,
        url: publicCodeforcesUrl(cleanPageUrl()),
        verdict: "",
        message: submissionStarted
          ? `${message}; check Codeforces before trying again`
          : message
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

  async function dispatchFragment() {
    const rawFragment = location.hash.startsWith("#") ? location.hash.slice(1) : "";
    const parameters = new URLSearchParams(rawFragment);
    if (!parameters.has("cfprobs")) {
      return;
    }

    const cleanUrl = new URL(location.href);
    cleanUrl.hash = "";
    cleanUrl.searchParams.delete("cfprobs_reload");
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

  void dispatchFragment();
})();
