"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const samples = require(path.resolve(__dirname, "../../src/browser/samples.js"));
const submission = require(path.resolve(__dirname, "../../src/browser/submission.js"));
const {createConnector, nextProblemUrl, parseRequest} = require(
  path.resolve(__dirname, "../../src/browser/connector.js")
);

const token = "a".repeat(64);
const artifact = {
  target: "71A",
  index: "A",
  language: "GNU C++20",
  source: "int main() {}"
};

function apiRecord(id, {
  target = "71A",
  handle = "panicsort",
  created = 20
} = {}) {
  const match = target.match(/^([0-9]+)([A-Za-z][A-Za-z0-9]*)$/);
  return {
    id,
    creationTimeSeconds: created,
    problem: {contestId: Number(match[1]), index: match[2]},
    author: {members: [{handle}]}
  };
}

function apiResponse(result) {
  const text = JSON.stringify({status: "OK", result});
  return {
    ok: true,
    headers: {get(name) { return name === "content-length" ? String(text.length) : null; }},
    async text() { return text; }
  };
}

function formDocument(onSubmit = () => {}, signedOut = false, contest = "") {
  const problem = {
    value: "",
    options: [{value: "A"}],
    dispatchEvent() {}
  };
  const problemField = contest ? "submittedProblemIndex" : "submittedProblemCode";
  const action = contest ? `/contest/${contest}/submit` : "/problemset/submit";
  const language = {
    value: "",
    options: [{disabled: false, textContent: "GNU G++20 13.2", value: "89"}],
    dispatchEvent() {}
  };
  const source = {value: "", dispatchEvent() {}};
  const csrf = {value: "csrf"};
  const submitter = {name: "", value: "Submit"};
  const form = {
    method: "post",
    querySelector(selector) {
      if (selector === `[name="${problemField}"]`) return problem;
      if (selector === '[name="programTypeId"]') return language;
      if (selector === '[name="source"]') return source;
      if (selector === '[name="csrf_token"]') return csrf;
      if (selector === '[name="cf-turnstile-response"]') return null;
      if (selector === '.cf-turnstile, [data-sitekey]') return null;
      if (selector === 'button[type="submit"], input[type="submit"]') return submitter;
      return null;
    },
    getAttribute(name) { return name === "action" ? action : null; },
    requestSubmit(value) { onSubmit(value); }
  };
  const status = {style: {}, textContent: ""};
  return {
    readyState: "complete",
    forms: [form],
    body: {appendChild() {}},
    documentElement: {outerHTML: "<html></html>"},
    createElement() { return {style: {}}; },
    getElementById(id) { return id === "cfx-connector-status" ? status : null; },
    querySelector(selector) {
      return signedOut && selector.includes("/enter") ? {} : null;
    },
    querySelectorAll(selector) {
      if (!signedOut && selector.includes("/profile/")) {
        return [{href: "https://codeforces.com/profile/panicsort"}];
      }
      return [];
    }
  };
}

function locationFor(value, origin = "https://codeforces.com") {
  const url = new URL(value, origin);
  return {
    origin: url.origin,
    href: url.href,
    pathname: url.pathname,
    search: url.search,
    hash: url.hash,
    assigned: "",
    replaced: "",
    assign(next) { this.assigned = next; },
    replace(next) { this.replaced = next; }
  };
}

function problemDocument() {
  const document = formDocument();
  const input = {innerText: "1\n"};
  const output = {innerText: "2\n"};
  const statement = {
    querySelector(selector) {
      if (selector === ".header .title") return {textContent: "A. Mirror test"};
      if (selector === ".header .time-limit") return {textContent: "1 second"};
      if (selector === ".header .memory-limit") return {textContent: "256 megabytes"};
      return null;
    },
    querySelectorAll(selector) {
      if (selector === ".sample-test .input pre") return [input];
      if (selector === ".sample-test .output pre") return [output];
      return [];
    }
  };
  document.querySelector = selector => selector === ".problem-statement" ? statement : null;
  return document;
}

function environment({document, location, sendMessage, fetch, clock, delay}) {
  return {
    chrome: {runtime: {sendMessage}},
    document,
    location,
    history: {state: null, replaceState() {}},
    Event: class Event {},
    fetch,
    now: () => clock.value,
    delay: delay || (async milliseconds => { clock.value += milliseconds; }),
    setTimeout
  };
}

async function submitFlowTest() {
  const clock = {value: 20000};
  const messages = [];
  let saved = false;
  let submitted = false;
  let savedValue;
  const document = formDocument(() => {
    assert.equal(saved, true, "pending state must be saved before requestSubmit");
    submitted = true;
  }, false, "71");
  const location = locationFor("/contest/71/submit");
  const connector = createConnector(environment({
    document,
    location,
    clock,
    async fetch(url, options) {
      const parsed = new URL(url);
      assert.equal(parsed.pathname, "/api/contest.status");
      assert.equal(parsed.searchParams.get("handle"), "panicsort");
      assert.equal(options.redirect, "error");
      return apiResponse([apiRecord(111)]);
    },
    async sendMessage(message) {
      messages.push(message);
      if (message.type === "cfx-submission-state") {
        if (message.action === "save") {
          saved = true;
          savedValue = message.value;
        }
        return {ok: true, value: null};
      }
      return message.route === "submission"
        ? {ok: true, status: 200, body: JSON.stringify(artifact)}
        : {ok: true, status: 200, body: "{}"};
    }
  }));

  await connector.handleSubmit({action: "submit", port: 32123, token});
  assert.equal(submitted, true);
  assert.deepEqual(savedValue.previousIds, ["111"]);
  assert.equal(savedValue.handle, "panicsort");
  assert.equal(savedValue.target, "71A");
  assert.equal(savedValue.submittedAtMillis, 20000);
  assert.deepEqual(messages.map(message => message.route || message.action), ["submission", "save"]);
}

async function resumeFlowTest(polls, expected, options = {}) {
  const clock = {value: 21000};
  const messages = [];
  const requestTimes = [];
  let poll = 0;
  const pending = {
    port: 32123,
    target: "71A",
    handle: "panicsort",
    previousIds: ["111"],
    submittedAtMillis: 20000,
    lastApiRequestMillis: 20000,
    expiresAtMillis: 30000
  };
  const document = formDocument(() => {}, options.signedOut);
  const page = options.page || "/problemset/status?my=on";
  const location = locationFor(page);
  location.pathname = new URL(location.href).pathname;
  location.search = new URL(location.href).search;
  const connector = createConnector(environment({
    document,
    location,
    clock,
    async fetch() {
      requestTimes.push(clock.value);
      const value = polls[poll++];
      if (value instanceof Error) throw value;
      assert.ok(value, "unexpected Codeforces API poll");
      return apiResponse(value);
    },
    async sendMessage(message) {
      messages.push(message);
      if (message.type === "cfx-submission-state") {
        return message.action === "load"
          ? {ok: true, value: {operation: token, value: pending}}
          : {ok: true, value: null};
      }
      return {ok: true, status: 200, body: "{}"};
    }
  }));

  await connector.resumePendingSubmission();
  const report = messages.find(message => message.route === "result");
  assert.ok(report, "submission identity result was not reported");
  const result = JSON.parse(report.body);
  assert.deepEqual(result, expected);
  assert.equal(messages.at(-1).action, "remove");
  return {location, messages, requestTimes};
}

async function fetchFlowTests() {
  const request = {action: "fetch", port: 32123, token};
  const pending = position => ({
    operation: token,
    value: {
      port: 32123,
      pathname: "/contest/71/problem/A",
      position,
      expiresAtMillis: 80000
    }
  });

  const initialMessages = [];
  const initialLocation = locationFor(
    `/contest/71/problem/A?cfx_reload=x#cfx=fetch&port=32123&token=${token}`
  );
  const initial = createConnector(environment({
    document: formDocument(),
    location: initialLocation,
    clock: {value: 20000},
    async fetch() { throw new Error("unexpected API fetch"); },
    async sendMessage(message) {
      initialMessages.push(message);
      if (message.type === "cfx-fetch-state") return {ok: true, value: null};
      return {ok: true, status: 200, body: "{}"};
    }
  }));
  await initial.dispatchFragment();
  assert.equal(initialLocation.replaced, "https://m1.codeforces.com/contest/71/problem/A");
  assert.deepEqual(initialMessages.map(message => message.route || message.action), [
    "save", "ready", "advance"
  ]);
  assert.equal(initialMessages[0].operation, token);
  assert.deepEqual(initialMessages[0].value, {
    port: 32123,
    pathname: "/contest/71/problem/A",
    position: 0
  });
  assert.equal(initialMessages[2].position, 1);

  const resumedMessages = [];
  const resumedLocation = locationFor(
    "/contest/71/problem/A", "https://m1.codeforces.com"
  );
  const resumed = createConnector(environment({
    document: formDocument(),
    location: resumedLocation,
    clock: {value: 20000},
    async fetch() { throw new Error("unexpected API fetch"); },
    async sendMessage(message) {
      resumedMessages.push(message);
      if (message.type === "cfx-fetch-state") {
        return {ok: true, value: message.action === "load" ? pending(1) : null};
      }
      return {ok: true, status: 200, body: "{}"};
    }
  }));
  assert.equal(await resumed.resumePendingFetch(), true);
  assert.equal(resumedLocation.replaced, "https://m2.codeforces.com/contest/71/problem/A");
  assert.deepEqual(resumedMessages.map(message => message.route || message.action), [
    "load", "ready", "advance"
  ]);
  assert.equal(resumedMessages.at(-1).position, 2);

  const challengeClock = {value: 20000};
  const challengeDocument = formDocument();
  challengeDocument.title = "Just a moment...";
  challengeDocument.body.textContent = "Enable JavaScript and cookies to continue";
  const challengeLocation = locationFor(
    "/contest/71/problem/A", "https://m2.codeforces.com"
  );
  const challenge = createConnector(environment({
    document: challengeDocument,
    location: challengeLocation,
    clock: challengeClock,
    async fetch() { throw new Error("unexpected API fetch"); },
    async sendMessage(message) {
      if (message.type === "cfx-fetch-state") {
        return {ok: true, value: message.action === "load" ? pending(2) : null};
      }
      return {ok: true, status: 200, body: "{}"};
    }
  }));
  await challenge.resumePendingFetch();
  assert.equal(challengeClock.value, 23000);
  assert.equal(challengeLocation.replaced, "https://m3.codeforces.com/contest/71/problem/A");

  const resolvedMessages = [];
  const resolvedClock = {value: 20000};
  const resolvedDocument = formDocument();
  resolvedDocument.title = "Just a moment...";
  resolvedDocument.body.textContent = "Your browser is being checked";
  const resolvedLocation = locationFor(
    "/contest/71/problem/A", "https://m2.codeforces.com"
  );
  const resolved = createConnector(environment({
    document: resolvedDocument,
    location: resolvedLocation,
    clock: resolvedClock,
    async delay(milliseconds) {
      resolvedClock.value += milliseconds;
      const ready = problemDocument();
      resolvedDocument.title = "";
      resolvedDocument.body.textContent = "";
      resolvedDocument.querySelector = ready.querySelector;
    },
    async fetch() { throw new Error("unexpected API fetch"); },
    async sendMessage(message) {
      resolvedMessages.push(message);
      if (message.type === "cfx-fetch-state") {
        return {ok: true, value: message.action === "load" ? pending(2) : null};
      }
      return {ok: true, status: 200, body: "{}"};
    }
  }));
  assert.equal(await resolved.resumePendingFetch(), true);
  assert.equal(resolvedClock.value, 23000);
  assert.equal(resolvedLocation.replaced, "");
  const resolvedPackage = JSON.parse(
    resolvedMessages.find(message => message.route === "fetch").body
  );
  assert.deepEqual(resolvedPackage.tests, [{input: "1\n", output: "2\n"}]);

  const successMessages = [];
  const successLocation = locationFor(
    "/contest/71/problem/A", "https://m3.codeforces.com"
  );
  const success = createConnector(environment({
    document: problemDocument(),
    location: successLocation,
    clock: {value: 20000},
    async fetch() { throw new Error("unexpected API fetch"); },
    async sendMessage(message) {
      successMessages.push(message);
      if (message.type === "cfx-fetch-state") {
        return {ok: true, value: message.action === "load" ? pending(3) : null};
      }
      return {ok: true, status: 200, body: "{}"};
    }
  }));
  assert.equal(await success.resumePendingFetch(), true);
  const fetched = successMessages.find(message => message.route === "fetch");
  assert.ok(fetched);
  assert.equal(JSON.parse(fetched.body).url, "https://codeforces.com/contest/71/problem/A");
  assert.deepEqual(successMessages.map(message => message.route || message.action), [
    "load", "ready", "fetch", "remove"
  ]);

  const failedMessages = [];
  const failedLocation = locationFor(
    "/contest/71/problem/A", "https://mirror.codeforces.com"
  );
  const failed = createConnector(environment({
    document: formDocument(),
    location: failedLocation,
    clock: {value: 20000},
    async fetch() { throw new Error("unexpected API fetch"); },
    async sendMessage(message) {
      failedMessages.push(message);
      if (message.type === "cfx-fetch-state") {
        return {ok: true, value: message.action === "load" ? pending(4) : null};
      }
      return {ok: true, status: 200, body: "{}"};
    }
  }));
  assert.equal(await failed.resumePendingFetch(), true);
  assert.equal(failedLocation.replaced, "");
  assert.deepEqual(failedMessages.map(message => message.route || message.action), [
    "load", "ready", "fetch-error", "remove"
  ]);
}

async function main() {
  assert.deepEqual(parseRequest(`cfx=submit&port=32123&token=${token}`), {
    action: "submit", port: 32123, token
  });
  assert.throws(() => parseRequest("cfx=submit&port=0&token=x"), /port/);
  assert.throws(
    () => parseRequest(`cfx=submit&cfx=fetch&port=32123&token=${token}`),
    /invalid connector cfx/
  );
  assert.throws(
    () => parseRequest(`cfx=submit&port=32123&token=${token}&extra=1`),
    /unknown connector parameter/
  );
  assert.equal(samples.parseTimeLimit("1.25 seconds"), 1250);
  assert.equal(samples.parseMemoryLimit("262144 kilobytes"), 256);
  assert.equal(samples.renderedSample({innerText: "1\r\n2\r"}), "1\n2\n");
  assert.equal(samples.cleanUrl({
    href: "https://m3.codeforces.com/contest/71/problem/A?__cf_chl_tk=secret#connector"
  }), "https://codeforces.com/contest/71/problem/A");
  const origins = [
    "https://codeforces.com",
    "https://m1.codeforces.com",
    "https://m2.codeforces.com",
    "https://m3.codeforces.com",
    "https://mirror.codeforces.com"
  ];
  for (let index = 0; index + 1 < origins.length; ++index) {
    assert.equal(
      nextProblemUrl(locationFor("/contest/71/problem/A", origins[index])),
      `${origins[index + 1]}/contest/71/problem/A`
    );
  }
  assert.equal(
    nextProblemUrl(locationFor("/contest/71/problem/A", origins[0]), 3),
    "https://mirror.codeforces.com/contest/71/problem/A"
  );
  assert.equal(
    nextProblemUrl(locationFor("/contest/71/problem/A/", origins[1]), 1),
    "https://m2.codeforces.com/contest/71/problem/A"
  );
  assert.equal(nextProblemUrl(locationFor(
    "/contest/71/problem/A", "https://evil.codeforces.com"
  )), "");

  const handleDocument = {
    querySelectorAll() {
      return [
        {href: "https://codeforces.com/profile/PanicSort"},
        {href: "/profile/panicsort"}
      ];
    }
  };
  assert.equal(submission.loggedInHandle(handleDocument, "https://codeforces.com"), "panicsort");
  assert.throws(() => submission.loggedInHandle({
    querySelectorAll() { return [{href: "https://example.com/profile/panicsort"}]; }
  }, "https://codeforces.com"), /not signed in/);
  assert.throws(() => submission.loggedInHandle({
    querySelectorAll() {
      return [{href: "/profile/one"}, {href: "/profile/two"}];
    }
  }, "https://codeforces.com"), /determine/);

  for (const action of [
    "?csrf_token=csrf",
    "submit?csrf_token=csrf",
    "/contest/71/submit?csrf_token=csrf",
    "https://codeforces.com/contest/71/submit?csrf_token=csrf"
  ]) {
    const relativeActionDocument = formDocument(() => {}, false, "71");
    relativeActionDocument.baseURI = "https://codeforces.com/contest/71/submit?cfx_reload=1";
    relativeActionDocument.forms[0].getAttribute = name => name === "action" ? action : null;
    assert.doesNotThrow(() => submission.prepareForm(
      artifact, relativeActionDocument, locationFor("/contest/71/submit"), class Event {}
    ));
  }
  const relativeSubmitterDocument = formDocument(() => {}, false, "71");
  relativeSubmitterDocument.baseURI = "https://codeforces.com/contest/71/submit";
  const relativeSubmitter = relativeSubmitterDocument.forms[0].querySelector(
    'button[type="submit"], input[type="submit"]'
  );
  relativeSubmitter.getAttribute = name =>
    name === "formaction" ? "?csrf_token=csrf" : null;
  assert.doesNotThrow(() => submission.prepareForm(
    artifact, relativeSubmitterDocument, locationFor("/contest/71/submit"), class Event {}
  ));

  const badActionDocument = formDocument(() => {}, false, "71");
  badActionDocument.forms[0].getAttribute = name =>
    name === "action" ? "/contest/72/submit" : null;
  assert.throws(() => submission.prepareForm(
    artifact, badActionDocument, locationFor("/contest/71/submit"), class Event {}
  ), /unexpected target/);
  const badSubmitterDocument = formDocument();
  const badSubmitter = badSubmitterDocument.forms[0].querySelector(
    'button[type="submit"], input[type="submit"]'
  );
  badSubmitter.getAttribute = name =>
    name === "formaction" ? "https://example.com/problemset/submit" : null;
  assert.throws(() => submission.prepareForm(
    artifact, badSubmitterDocument, locationFor("/problemset/submit"), class Event {}
  ), /unexpected target/);
  const badBaseDocument = formDocument(() => {}, false, "71");
  badBaseDocument.baseURI = "https://example.com/contest/71/submit";
  badBaseDocument.forms[0].getAttribute = name => name === "action" ? "submit" : null;
  assert.throws(() => submission.prepareForm(
    artifact, badBaseDocument, locationFor("/contest/71/submit"), class Event {}
  ), /unexpected target/);

  const pending = {
    target: "71A",
    handle: "panicsort",
    previousIds: ["111"],
    submittedAtMillis: 20000
  };
  const noise = [
    apiRecord(111),
    apiRecord(222, {created: 19}),
    apiRecord(333, {handle: "someoneElse"}),
    apiRecord(444, {target: "71B"})
  ];
  assert.equal(submission.identifySubmission(noise, pending, 25000), null);
  assert.deepEqual(
    submission.identifySubmission([...noise, apiRecord(555)], pending, 25000),
    {
      submissionId: "555",
      handle: "panicsort",
      submittedAtMillis: 20000,
      url: "https://codeforces.com/contest/71/submission/555"
    }
  );
  assert.throws(
    () => submission.identifySubmission([apiRecord(555), apiRecord(556)], pending, 25000),
    /ambiguous/
  );
  await assert.rejects(
    submission.recentSubmissions(async url => ({
      ok: true,
      redirected: true,
      url: url.replace("codeforces.com", "example.com"),
      async json() { return {status: "OK", result: []}; }
    }), "71A", "panicsort", "https://codeforces.com"),
    /redirected unexpectedly/
  );
  await assert.rejects(
    submission.recentSubmissions(async () => ({
      ok: true,
      headers: {get: () => String(256 * 1024 + 1)},
      async text() { throw new Error("oversized body must not be read"); }
    }), "71A", "panicsort", "https://codeforces.com"),
    /response is too large/
  );
  assert.throws(
    () => submission.apiRecords({status: "OK", result: Array(101).fill({})}),
    /invalid data/
  );

  await submitFlowTest();
  await fetchFlowTests();
  const accepted = await resumeFlowTest([[apiRecord(555)], [apiRecord(555)]], {
    ok: true,
    submissionId: "555",
    handle: "panicsort",
    submittedAtMillis: 20000
  });
  assert.equal(accepted.location.assigned, "https://codeforces.com/contest/71/submission/555");
  const acceptedBody = accepted.messages.find(message => message.route === "result").body;
  assert.doesNotMatch(acceptedBody, /verdict|passedTest|memory|timeConsumed/);

  await resumeFlowTest([[apiRecord(555), apiRecord(556)]], {
    ok: false,
    unknown: true,
    message: "submission identity is ambiguous; check Codeforces before trying again"
  });

  await resumeFlowTest([[apiRecord(555)], [apiRecord(555), apiRecord(556)]], {
    ok: false,
    unknown: true,
    message: "submission identity is ambiguous; check Codeforces before trying again"
  });
  await resumeFlowTest([[apiRecord(555)], [apiRecord(556)]], {
    ok: false,
    unknown: true,
    message: "submission identity is ambiguous; check Codeforces before trying again"
  });

  const recovered = await resumeFlowTest([
    new Error("temporary API failure"),
    [apiRecord(555)],
    [apiRecord(555)]
  ], {
    ok: true,
    submissionId: "555",
    handle: "panicsort",
    submittedAtMillis: 20000
  });
  assert.ok(recovered.requestTimes.every((value, index, times) =>
    index === 0 || value - times[index - 1] >= 2100));

  const signedOut = await resumeFlowTest([], {
    ok: false,
    unknown: false,
    message: "Codeforces login expired"
  }, {page: "/enter?back=%2Fproblemset%2Fsubmit", signedOut: true});
  assert.equal(signedOut.requestTimes.length, 0);
  const signedOutStatus = await resumeFlowTest([], {
    ok: false,
    unknown: false,
    message: "Codeforces login expired"
  }, {page: "/problemset/status?my=on", signedOut: true});
  assert.equal(signedOutStatus.requestTimes.length, 0);

  const connectorSource = fs.readFileSync(
    path.resolve(__dirname, "../../src/browser/connector.js"), "utf8"
  );
  assert.doesNotMatch(connectorSource, /sessionStorage|submissionApiOutcome|passedTestCount/);
  console.log("content connector tests passed");
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
