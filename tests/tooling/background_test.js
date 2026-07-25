"use strict";

const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");

let listener;
const requests = [];
global.chrome = {
  runtime: {
    id: "abcdefghijklmnopabcdefghijklmnop",
    onMessage: {
      addListener(value) {
        listener = value;
      }
    }
  }
};
global.fetch = async (url, options) => {
  requests.push({url, options});
  return {
    status: 200,
    text: async () => "{\"ok\":true}"
  };
};

require(path.resolve(__dirname, "../../browser/background.js"));
assert.equal(typeof listener, "function");

const sender = {
  id: chrome.runtime.id,
  frameId: 0,
  url: "https://codeforces.com/contest/71/problem/A"
};
const request = {
  type: "cfx-local-request",
  port: 32123,
  token: "a".repeat(64),
  route: "fetch",
  method: "POST",
  body: "{\"tests\":[]}"
};

function send(message, source = sender) {
  return new Promise(resolve => {
    assert.equal(listener(message, source, resolve), true);
  });
}

async function main() {
  const manifest = JSON.parse(
    fs.readFileSync(path.resolve(__dirname, "../../browser/manifest.json"), "utf8")
  );
  const contentScriptMatches = manifest.content_scripts.flatMap(script => script.matches || []);
  for (const match of [
    "https://codeforces.com/problemset/status*",
    "https://codeforces.com/contest/*/my*",
    "https://codeforces.com/contest/*/status*",
    "https://codeforces.com/contest/*/submission/*",
    "https://codeforces.com/problemset/submission/*/*",
    "https://codeforces.com/submissions/*"
  ]) {
    assert.ok(contentScriptMatches.includes(match), `missing content-script match: ${match}`);
  }
  const expectedId = fs
    .readFileSync(path.resolve(__dirname, "../../browser/extension-id"), "utf8")
    .trim();
  const digest = crypto.createHash("sha256").update(Buffer.from(manifest.key, "base64")).digest();
  const derivedId = Array.from(digest.subarray(0, 16), byte =>
    `${String.fromCharCode(97 + (byte >> 4))}${String.fromCharCode(97 + (byte & 15))}`
  ).join("");
  assert.equal(derivedId, expectedId);

  const accepted = await send(request);
  assert.deepEqual(accepted, {ok: true, status: 200, body: "{\"ok\":true}"});
  assert.equal(requests.length, 1);
  assert.equal(requests[0].url, `http://127.0.0.1:32123/fetch/${"a".repeat(64)}`);
  assert.equal(requests[0].options.credentials, "omit");
  assert.equal(requests[0].options.body, request.body);
  assert.equal(requests[0].options.headers["X-Cfx-Extension"], chrome.runtime.id);

  const wrongOrigin = await send(request, {
    ...sender,
    url: "https://example.com/contest/71/problem/A"
  });
  assert.equal(wrongOrigin.ok, false);
  assert.match(wrongOrigin.error, /Codeforces/);

  const childFrame = await send(request, {...sender, frameId: 1});
  assert.equal(childFrame.ok, false);

  const archiveSubmit = await send(
    {...request, route: "ready", method: "GET", body: ""},
    {...sender, url: "https://codeforces.com/problemset/submit"}
  );
  assert.equal(archiveSubmit.ok, true);

  const signInRedirect = await send(
    {...request, route: "result", body: '{"ok":false}'},
    {...sender, url: "https://codeforces.com/enter?back=%2Fproblemset%2Fsubmit"}
  );
  assert.equal(signInRedirect.ok, true);

  const resultPages = [
    "https://codeforces.com/problemset/status?my=on",
    "https://codeforces.com/contest/71/my",
    "https://codeforces.com/contest/71/status",
    "https://codeforces.com/contest/71/submission/123456789",
    "https://codeforces.com/problemset/submission/71/123456789",
    "https://codeforces.com/submissions/panicsort"
  ];
  for (const url of resultPages) {
    const response = await send(
      {...request, route: "result", body: '{"ok":true}'},
      {...sender, url}
    );
    assert.equal(response.ok, true, `result page was rejected: ${url}`);
  }

  const unrelatedPage = await send(
    {...request, route: "result", body: '{"ok":true}'},
    {...sender, url: "https://codeforces.com/settings/general"}
  );
  assert.equal(unrelatedPage.ok, false);

  const wrongRoute = await send({...request, route: "anything"});
  assert.equal(wrongRoute.ok, false);
  assert.match(wrongRoute.error, /route/);
  assert.equal(requests.length, 3 + resultPages.length);

  console.log("background connector tests passed");
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
