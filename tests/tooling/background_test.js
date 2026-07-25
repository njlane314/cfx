"use strict";

const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");

let listener;
let alarmListener;
const requests = [];
const stored = new Map();
const alarms = new Map();
global.chrome = {
  runtime: {
    id: "abcdefghijklmnopabcdefghijklmnop",
    onMessage: {addListener(value) { listener = value; }}
  },
  alarms: {
    onAlarm: {addListener(value) { alarmListener = value; }},
    async create(name, details) { alarms.set(name, details); },
    async clear(name) { return alarms.delete(name); }
  },
  storage: {
    session: {
      async get() { return Object.fromEntries(stored); },
      async set(values) {
        for (const [key, value] of Object.entries(values)) stored.set(key, value);
      },
      async remove(keys) {
        for (const key of Array.isArray(keys) ? keys : [keys]) stored.delete(key);
      }
    }
  }
};
global.fetch = async (url, options) => {
  requests.push({url, options});
  return {status: 200, text: async () => "{\"ok\":true}"};
};

const background = require(path.resolve(__dirname, "../../browser/background.js"));
background.listen();
assert.equal(typeof listener, "function");
assert.equal(typeof alarmListener, "function");

const sender = {
  id: chrome.runtime.id,
  frameId: 0,
  tab: {id: 7, url: "https://codeforces.com/contest/71/problem/A"},
  url: "https://codeforces.com/contest/71/problem/A"
};
const local = {
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

function pending(overrides = {}) {
  return {
    port: 32123,
    target: "71A",
    handle: "panicsort",
    previousIds: ["111", "222"],
    submittedAtMillis: Date.now(),
    lastApiRequestMillis: Date.now(),
    ...overrides
  };
}

async function main() {
  const manifest = JSON.parse(
    fs.readFileSync(path.resolve(__dirname, "../../browser/manifest.json"), "utf8")
  );
  assert.deepEqual(manifest.permissions, ["alarms", "storage"]);
  assert.deepEqual(manifest.content_scripts[0].js, ["samples.js", "submission.js", "connector.js"]);
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

  const accepted = await send(local);
  assert.deepEqual(accepted, {ok: true, status: 200, body: "{\"ok\":true}"});
  assert.equal(requests[0].url, `http://127.0.0.1:32123/fetch/${"a".repeat(64)}`);
  assert.equal(requests[0].options.credentials, "omit");
  assert.equal(requests[0].options.body, local.body);
  assert.equal(requests[0].options.headers["Content-Type"], "application/json");
  assert.equal(requests[0].options.headers["X-Cfx-Extension"], chrome.runtime.id);

  const operationA = "a".repeat(64);
  const operationB = "b".repeat(64);
  const state = (action, operation, value) => ({
    type: "cfx-submission-state",
    action,
    ...(operation ? {operation} : {}),
    ...(value ? {value} : {})
  });
  assert.deepEqual(await send(state("save", operationA, pending())), {ok: true, value: null});
  const loaded = await send(state("load"));
  assert.equal(loaded.ok, true);
  assert.equal(loaded.value.operation, operationA);
  assert.equal(loaded.value.value.handle, "panicsort");
  assert.ok(loaded.value.value.expiresAtMillis > Date.now());
  assert.deepEqual(alarms.get(background.alarmName(background.stateKey(7, operationA))), {
    when: loaded.value.value.expiresAtMillis
  });

  const secondOperation = await send(state("save", operationB, pending()));
  assert.equal(secondOperation.ok, false);
  assert.match(secondOperation.error, /another submission operation/);

  const otherTab = {...sender, tab: {...sender.tab, id: 8}};
  assert.equal((await send(state("save", operationA, pending()), otherTab)).ok, true);
  assert.ok(stored.has(background.stateKey(7, operationA)));
  assert.ok(stored.has(background.stateKey(8, operationA)));
  await send(state("remove", operationA));
  assert.equal(stored.has(background.stateKey(7, operationA)), false);
  assert.equal(alarms.has(background.alarmName(background.stateKey(7, operationA))), false);
  assert.equal(stored.has(background.stateKey(8, operationA)), true);
  assert.equal((await send(state("save", operationB, pending()))).ok, true);
  assert.ok(stored.has(background.stateKey(7, operationB)));

  const staleSender = {...sender, tab: {...sender.tab, id: 9}};
  const staleKey = background.stateKey(9, operationA);
  stored.set(staleKey, {...pending(), expiresAtMillis: Date.now() - 1});
  alarms.set(background.alarmName(staleKey), {when: Date.now() - 1});
  assert.deepEqual(await send(state("load"), staleSender), {ok: true, value: null});
  assert.equal(stored.has(staleKey), false);
  assert.equal(alarms.has(background.alarmName(staleKey)), false);

  const concurrentSender = {...sender, tab: {...sender.tab, id: 11}};
  const concurrent = await Promise.all([
    send(state("save", "c".repeat(64), pending()), concurrentSender),
    send(state("save", "d".repeat(64), pending()), concurrentSender)
  ]);
  assert.equal(concurrent.filter(result => result.ok).length, 1);
  assert.equal(concurrent.filter(result => !result.ok).length, 1);
  const concurrentPrefix = "cfx:submission:11:";
  const concurrentKeys = [...stored.keys()].filter(key => key.startsWith(concurrentPrefix));
  assert.equal(concurrentKeys.length, 1);
  assert.equal(alarms.has(background.alarmName(concurrentKeys[0])), true);

  const otherKey = background.stateKey(8, operationA);
  alarmListener({name: background.alarmName(otherKey)});
  await background.stateIdle();
  assert.equal(stored.has(otherKey), false);
  assert.equal(alarms.has(background.alarmName(otherKey)), false);

  const invalidState = await send(
    state("save", operationA, pending({previousIds: ["1", "1"]})),
    {...sender, tab: {...sender.tab, id: 10}}
  );
  assert.equal(invalidState.ok, false);
  assert.match(invalidState.error, /invalid pending/);

  const wrongOrigin = await send(local, {...sender, url: "https://example.com/contest/71/problem/A"});
  assert.equal(wrongOrigin.ok, false);
  assert.match(wrongOrigin.error, /Codeforces/);
  assert.equal((await send(local, {...sender, frameId: 1})).ok, false);
  assert.equal((await send(local, {...sender, tab: undefined})).ok, false);

  const archiveSubmit = await send(
    {...local, route: "ready", method: "GET", body: ""},
    {...sender, url: "https://codeforces.com/problemset/submit"}
  );
  assert.equal(archiveSubmit.ok, true);
  const unrelatedPage = await send(
    {...local, route: "result", body: '{"ok":false}'},
    {...sender, url: "https://codeforces.com/settings/general"}
  );
  assert.equal(unrelatedPage.ok, false);
  const wrongRoute = await send({...local, route: "anything"});
  assert.equal(wrongRoute.ok, false);
  assert.match(wrongRoute.error, /route/);

  console.log("background connector tests passed");
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
