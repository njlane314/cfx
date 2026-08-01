"use strict";

const assert = require("node:assert/strict");
const childProcess = require("node:child_process");
const path = require("node:path");

const root = path.resolve(__dirname, "../..");

const archive = childProcess.execFileSync("bash", [path.join(root, "src/browser/package.sh")], {
    cwd: root,
    encoding: "utf8"
}).trim();
const entries = childProcess.execFileSync("unzip", ["-Z1", archive], {encoding: "utf8"})
    .trim().split("\n").sort();
assert.deepEqual(entries, [
    "background.js",
    "connector.js",
    "manifest.json",
    "samples.js",
    "submission.js"
]);

const manifest = JSON.parse(childProcess.execFileSync(
    "unzip", ["-p", archive, "manifest.json"], {encoding: "utf8"}
));
assert.equal(manifest.icons, undefined);

console.log("browser package tests passed");
