(function(root, factory) {
  "use strict";

  const api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.CfxSubmission = api;
  }
})(globalThis, () => {
  "use strict";

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
      const match = new URL(link.href, origin).pathname.match(/^\/profile\/([^/]+)\/?$/i);
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
      if (/^[A-Za-z0-9_.-]+$/.test(handle)) handles.set(handle.toLowerCase(), handle);
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

    const action = new URL(
      form.getAttribute("action") || `${location.pathname}${location.search}`,
      location.origin
    );
    if (action.origin !== location.origin || (form.method || "get").toLowerCase() !== "post") {
      throw new Error("Codeforces submission form has an unexpected target");
    }
    const csrf = form.querySelector('[name="csrf_token"]');
    if (!String(csrf?.value || "").trim() && !action.searchParams.get("csrf_token")) {
      throw new Error("Codeforces submission form has no CSRF token; reload and sign in");
    }
    return {form, submitter: form.querySelector('button[type="submit"], input[type="submit"]')};
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
    if (!payload || payload.status !== "OK" || !Array.isArray(payload.result)) {
      const comment = String(payload?.comment || "").replace(/\s+/g, " ").trim();
      throw new Error(comment ? `Codeforces API: ${comment}` : "Codeforces API returned invalid data");
    }
    return payload.result;
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
    const response = await fetch(statusUrl(target, handle, origin), {
      credentials: "omit",
      cache: "no-store",
      redirect: "follow",
      referrerPolicy: "no-referrer"
    });
    if (!response.ok) throw new Error(`Codeforces API returned HTTP ${response.status}`);
    return apiRecords(await response.json());
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
});
