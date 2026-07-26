"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const samples = require(path.resolve(__dirname, "../../src/browser/samples.js"));
const submission = require(path.resolve(__dirname, "../../src/browser/submission.js"));
const {createConnector, parseRequest} = require(
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

function formDocument(onSubmit = () => {}, signedOut = false) {
  const problem = {value: "", dispatchEvent() {}};
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
      if (selector === '[name="submittedProblemCode"]') return problem;
      if (selector === '[name="programTypeId"]') return language;
      if (selector === '[name="source"]') return source;
      if (selector === '[name="csrf_token"]') return csrf;
      if (selector === '[name="cf-turnstile-response"]') return null;
      if (selector === '.cf-turnstile, [data-sitekey]') return null;
      if (selector === 'button[type="submit"], input[type="submit"]') return submitter;
      return null;
    },
    getAttribute(name) { return name === "action" ? "/problemset/submit" : null; },
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

function locationFor(pathname) {
  return {
    origin: "https://codeforces.com",
    href: `https://codeforces.com${pathname}`,
    pathname,
    search: "",
    hash: "",
    assigned: "",
    assign(value) { this.assigned = value; }
  };
}

function environment({document, location, sendMessage, fetch, clock}) {
  return {
    chrome: {runtime: {sendMessage}},
    document,
    location,
    history: {state: null, replaceState() {}},
    Event: class Event {},
    fetch,
    now: () => clock.value,
    delay: async milliseconds => { clock.value += milliseconds; },
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
  });
  const location = locationFor("/problemset/submit");
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

  const badActionDocument = formDocument();
  badActionDocument.forms[0].getAttribute = name =>
    name === "action" ? "/settings/general" : null;
  assert.throws(() => submission.prepareForm(
    artifact, badActionDocument, locationFor("/problemset/submit"), class Event {}
  ), /unexpected target/);
  const badSubmitterDocument = formDocument();
  const badSubmitter = badSubmitterDocument.forms[0].querySelector(
    'button[type="submit"], input[type="submit"]'
  );
  badSubmitter.getAttribute = name => name === "formaction" ? "/settings/general" : null;
  assert.throws(() => submission.prepareForm(
    artifact, badSubmitterDocument, locationFor("/problemset/submit"), class Event {}
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
