"use strict";

const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const sourcePath = path.resolve(__dirname, "../../browser/connector.js");
const source = fs.readFileSync(sourcePath, "utf8");
const launchMarker = "  const launchParameters = new URLSearchParams(";
const launchPosition = source.lastIndexOf(launchMarker);
assert.notEqual(launchPosition, -1, "connector launch marker is missing");

const instrumented = `${source.slice(0, launchPosition)}
  globalThis.__cfxConnectorTest = {
    clearPendingSubmission,
    handleSubmit,
    parseConsumedMemory,
    parseConsumedTime,
    pendingSubmission,
    resumePendingSubmission,
    submissionApiOutcome,
    submissionOutcome
  };
})();
`;

const values = new Map();
const sessionStorage = {
  getItem(key) {
    return values.has(key) ? values.get(key) : null;
  },
  removeItem(key) {
    values.delete(key);
  },
  setItem(key, value) {
    values.set(key, String(value));
  }
};

const statusBox = {style: {}, textContent: ""};
const problem = {value: "", dispatchEvent() {}};
const language = {
  value: "",
  options: [
    {disabled: false, textContent: "GNU G++20 13.2 (64 bit, winlibs)", value: "89"}
  ],
  dispatchEvent() {}
};
const sourceField = {value: "", dispatchEvent() {}};
const csrf = {value: "csrf"};
const submitter = {name: "", value: "Submit"};
let requestSubmit = () => {};
const form = {
  method: "post",
  querySelector(selector) {
    if (selector === '[name="submittedProblemCode"]') return problem;
    if (selector === '[name="programTypeId"]') return language;
    if (selector === '[name="source"]') return sourceField;
    if (selector === '[name="csrf_token"]') return csrf;
    if (selector === '[name="cf-turnstile-response"]') return null;
    if (selector === '.cf-turnstile, [data-sitekey]') return null;
    if (selector === 'button[type="submit"], input[type="submit"]') return submitter;
    return null;
  },
  getAttribute(name) {
    return name === "action" ? "/problemset/submit" : null;
  },
  requestSubmit(value) {
    requestSubmit(value);
  }
};

let outcomeDocument;
const document = {
  readyState: "complete",
  forms: [form],
  documentElement: {outerHTML: "<html></html>"},
  body: {appendChild() {}},
  createElement() {
    return {style: {}, setAttribute() {}};
  },
  getElementById(id) {
    return id === "cfx-connector-status" ? statusBox : null;
  },
  querySelector() {
    return null;
  },
  querySelectorAll() {
    return [];
  }
};

const location = {
  origin: "https://codeforces.com",
  href: "https://codeforces.com/problemset/submit",
  pathname: "/problemset/submit",
  search: "",
  hash: "",
  assigned: "",
  assign(value) {
    this.assigned = value;
  }
};

const messages = [];
let apiPayloads = [];
let detailFetches = 0;
const artifact = {
  target: "71A",
  index: "A",
  language: "GNU C++20",
  source: "int main() {}"
};
const chrome = {
  runtime: {
    async sendMessage(message) {
      messages.push(message);
      return message.route === "submission"
        ? {ok: true, status: 200, body: JSON.stringify(artifact)}
        : {ok: true, status: 200, body: "{}"};
    }
  }
};

class DOMParser {
  parseFromString() {
    return outcomeDocument;
  }
}

async function fetch(url) {
  const parsed = new URL(url);
  if (/\/submission\/[0-9]+$/.test(parsed.pathname)) {
    detailFetches += 1;
    return {
      ok: true,
      status: 200,
      url: parsed.href,
      async text() {
        return "<html></html>";
      }
    };
  }
  assert.equal(parsed.pathname, "/api/contest.status");
  assert.equal(parsed.searchParams.get("contestId"), "71");
  assert.equal(parsed.searchParams.get("handle"), "panicsort");
  assert.ok(apiPayloads.length, "unexpected Codeforces API request");
  const payload = apiPayloads.shift();
  return {
    ok: true,
    status: 200,
    async json() {
      return payload;
    }
  };
}

const context = {
  URL,
  URLSearchParams,
  Date,
  DOMParser,
  Event: class Event {},
  JSON,
  Map,
  Number,
  Set,
  String,
  Array,
  chrome,
  clearInterval,
  crypto: crypto.webcrypto,
  document,
  fetch,
  history: {replaceState() {}},
  location,
  sessionStorage,
  setInterval,
  setTimeout(callback) {
    callback();
    return 1;
  }
};
context.globalThis = context;
vm.runInNewContext(instrumented, context, {filename: sourcePath});
const connector = context.__cfxConnectorTest;

function bridgeRequest(token = "a".repeat(64)) {
  return {action: "submit", port: 32123, token};
}

function pendingValue(overrides = {}) {
  return JSON.stringify({
    port: 32123,
    token: "a".repeat(64),
    target: "71A",
    knownSubmissionIds: [],
    createdAt: Date.now(),
    submissionId: "",
    submissionUrl: "",
    authorHandle: "",
    ...overrides
  });
}

function resultDocument({
  id = "123456789",
  target = "71A",
  verdict = "OK",
  verdictText = "Accepted",
  time = "46 ms",
  memory = "100 KB",
  author = "panicsort"
} = {}) {
  const targetMatch = target.match(/^([0-9]+)([A-Za-z][A-Za-z0-9]*)$/);
  const submissionLink = {
    href: `https://codeforces.com/contest/${targetMatch[1]}/submission/${id}`
  };
  const problemLink = {
    href: `https://codeforces.com/contest/${targetMatch[1]}/problem/${targetMatch[2]}`
  };
  const authorLink = {href: `https://codeforces.com/profile/${author}`};
  const row = {
    querySelector(selector) {
      if (selector === 'a[href*="/submission/"]') return submissionLink;
      if (selector.includes('a[href*="/problem/"]')) return problemLink;
      if (selector.includes("/profile/")) return authorLink;
      if (selector.includes("submissionverdict")) {
        return {
          textContent: verdictText,
          getAttribute(name) {
            return name === "submissionverdict" ? verdict : null;
          }
        };
      }
      if (selector === ".time-consumed-cell") return {textContent: time};
      if (selector === ".memory-consumed-cell") return {textContent: memory};
      return null;
    }
  };
  return {
    querySelector(selector) {
      return row.querySelector(selector);
    },
    querySelectorAll(selector) {
      return selector.startsWith("tr[data-submission-id]") ? [row] : [];
    }
  };
}

async function main() {
  assert.equal(connector.parseConsumedTime("46 ms"), 46);
  assert.equal(connector.parseConsumedTime("1.25 seconds"), 1250);
  assert.equal(connector.parseConsumedTime("not measured"), null);
  assert.equal(connector.parseConsumedMemory("100 KB"), 102400);
  assert.equal(connector.parseConsumedMemory("1.5 MiB"), 1572864);
  assert.equal(connector.parseConsumedMemory("not measured"), null);

  let submitterSeen;
  let pendingSeen;
  requestSubmit = value => {
    submitterSeen = value;
    pendingSeen = sessionStorage.getItem("cfx-pending-submission-v1");
  };
  await connector.handleSubmit(bridgeRequest());
  assert.equal(submitterSeen, submitter);
  assert.ok(pendingSeen, "pending state was not written before native submission");
  assert.deepEqual(messages.map(message => message.route), ["submission"]);

  messages.length = 0;
  connector.clearPendingSubmission();
  requestSubmit = () => {
    throw new Error("native submit failed");
  };
  await connector.handleSubmit(bridgeRequest("b".repeat(64)));
  assert.equal(sessionStorage.getItem("cfx-pending-submission-v1"), null);
  assert.deepEqual(messages.map(message => message.route), ["submission", "result"]);
  const failedResult = JSON.parse(messages[1].body);
  assert.equal(failedResult.ok, false);
  assert.equal(failedResult.unknown, false);
  assert.match(failedResult.message, /native submit failed/);

  messages.length = 0;
  sessionStorage.setItem("cfx-pending-submission-v1", pendingValue());
  location.href = "https://codeforces.com/problemset/status?my=on";
  location.pathname = "/problemset/status";
  location.search = "?my=on";
  location.assigned = "";
  outcomeDocument = resultDocument({time: "31 ms", memory: "72 KB"});
  detailFetches = 0;
  apiPayloads = [
    {
      status: "OK",
      result: [{id: 123456789, verdict: "TESTING", passedTestCount: 7,
        timeConsumedMillis: 0, memoryConsumedBytes: 0, testset: "TESTS"}]
    },
    {
      status: "OK",
      result: [{id: 123456789, verdict: "OK", passedTestCount: 20,
        timeConsumedMillis: 31, memoryConsumedBytes: 102400, testset: "TESTS"}]
    }
  ];
  await connector.resumePendingSubmission();
  assert.equal(sessionStorage.getItem("cfx-pending-submission-v1"), null);
  assert.equal(messages.length, 1);
  const acceptedResult = JSON.parse(messages[0].body);
  assert.equal(acceptedResult.ok, true);
  assert.equal(acceptedResult.unknown, false);
  assert.equal(
    acceptedResult.url,
    "https://codeforces.com/contest/71/submission/123456789"
  );
  assert.equal(acceptedResult.submissionId, "123456789");
  assert.equal(acceptedResult.verdict, "OK");
  assert.equal(acceptedResult.verdictText, "Accepted");
  assert.equal(acceptedResult.passedTestCount, 20);
  assert.equal(acceptedResult.timeConsumedMillis, 31);
  assert.equal(acceptedResult.memoryConsumedBytes, 73728);
  assert.ok(Number.isSafeInteger(acceptedResult.judgingWaitMillis));
  assert.ok(acceptedResult.judgingWaitMillis >= 0);
  assert.equal(apiPayloads.length, 0);
  assert.equal(detailFetches, 1);
  assert.equal(location.assigned, acceptedResult.url);

  messages.length = 0;
  location.assigned = "";
  outcomeDocument = resultDocument({target: "4A"});
  const mismatchedResult = connector.submissionOutcome(
    {ok: true, status: 200, url: location.href},
    "<html></html>",
    new Set(),
    "71A"
  );
  assert.equal(mismatchedResult.pending, true);
  assert.equal(messages.length, 0);
  assert.equal(location.assigned, "");

  outcomeDocument = resultDocument({id: "111111111", target: "4A"});
  const unrelatedDirectResult = connector.submissionOutcome(
    {
      ok: true,
      status: 200,
      url: "https://codeforces.com/contest/4/submission/111111111"
    },
    "<html></html>",
    new Set(),
    "71A"
  );
  assert.equal(unrelatedDirectResult.pending, true);
  assert.equal(unrelatedDirectResult.submissionId, "");

  outcomeDocument = resultDocument({
    verdict: "TESTING",
    verdictText: "Running on test 7",
    time: "0 ms",
    memory: "0 KB"
  });
  const testingResult = connector.submissionOutcome(
    {ok: true, status: 200, url: location.href},
    "<html></html>",
    new Set(),
    "71A"
  );
  assert.equal(testingResult.pending, true);
  assert.equal(testingResult.submissionId, "123456789");
  assert.equal(testingResult.verdict, "TESTING");
  assert.equal(testingResult.verdictText, "Running on test 7");
  assert.equal(testingResult.timeConsumedMillis, undefined);
  assert.equal(testingResult.memoryConsumedBytes, undefined);

  outcomeDocument = resultDocument({
    id: "987654321",
    verdict: "TIME_LIMIT_EXCEEDED",
    verdictText: "Time limit exceeded on test 32",
    time: "1000 ms",
    memory: "256 KB"
  });
  const tleResult = connector.submissionOutcome(
    {ok: true, status: 200, url: location.href},
    "<html></html>",
    new Set(),
    "71A"
  );
  assert.equal(tleResult.ok, true);
  assert.equal(tleResult.unknown, false);
  assert.equal(tleResult.submissionId, "987654321");
  assert.equal(tleResult.verdict, "TIME_LIMIT_EXCEEDED");
  assert.equal(tleResult.verdictText, "Time limit exceeded on test 32");
  assert.equal(tleResult.timeConsumedMillis, 1000);
  assert.equal(tleResult.memoryConsumedBytes, 262144);

  const apiTle = connector.submissionApiOutcome(
    {
      status: "OK",
      result: [
        {id: 111111111, verdict: "OK", passedTestCount: 20,
          timeConsumedMillis: 5, memoryConsumedBytes: 1024, testset: "TESTS"},
        {id: 987654321, verdict: "TIME_LIMIT_EXCEEDED", passedTestCount: 31,
          timeConsumedMillis: 1000, memoryConsumedBytes: 262144, testset: "TESTS"}
      ]
    },
    {
      submissionId: "987654321",
      submissionUrl: "https://codeforces.com/contest/71/submission/987654321",
      createdAt: 1000
    },
    3500
  );
  assert.equal(apiTle.verdictText, "Time Limit Exceeded");
  assert.equal(apiTle.passedTestCount, 31);
  assert.equal(apiTle.judgingWaitMillis, 2500);

  const pretests = connector.submissionApiOutcome(
    {
      status: "OK",
      result: [{id: 123456789, verdict: "OK", passedTestCount: 5,
        timeConsumedMillis: 10, memoryConsumedBytes: 1024, testset: "PRETESTS"}]
    },
    {
      submissionId: "123456789",
      submissionUrl: "https://codeforces.com/contest/71/submission/123456789",
      createdAt: 0
    },
    100
  );
  assert.equal(pretests.verdictText, "Accepted (pretests)");

  assert.throws(
    () => connector.submissionApiOutcome(
      {
        status: "OK",
        result: [{id: 123456789, verdict: "OK", passedTestCount: 1.5,
          timeConsumedMillis: 10, memoryConsumedBytes: 1024, testset: "TESTS"}]
      },
      {
        submissionId: "123456789",
        submissionUrl: "https://codeforces.com/contest/71/submission/123456789",
        createdAt: 0
      },
      100
    ),
    /invalid passedTestCount/
  );

  sessionStorage.setItem(
    "cfx-pending-submission-v1",
    pendingValue({
      submissionId: "123456789",
      submissionUrl: "https://example.com/contest/71/submission/123456789",
      authorHandle: "panicsort"
    })
  );
  assert.equal(connector.pendingSubmission(), null);
  assert.equal(sessionStorage.getItem("cfx-pending-submission-v1"), null);

  sessionStorage.setItem(
    "cfx-pending-submission-v1",
    pendingValue({createdAt: Date.now() - 5 * 60 * 1000})
  );
  assert.equal(connector.pendingSubmission(), null);
  assert.equal(sessionStorage.getItem("cfx-pending-submission-v1"), null);

  console.log("content connector tests passed");
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
