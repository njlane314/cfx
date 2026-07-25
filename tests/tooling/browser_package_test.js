"use strict";

const assert = require("node:assert/strict");
const childProcess = require("node:child_process");
const path = require("node:path");

const root = path.resolve(__dirname, "../..");
const {render} = require(path.join(root, "browser/icon.js"));

function dimensions(png) {
    assert.deepEqual(png.subarray(0, 8), Buffer.from("89504e470d0a1a0a", "hex"));
    assert.equal(png.subarray(12, 16).toString("ascii"), "IHDR");
    return [png.readUInt32BE(16), png.readUInt32BE(20)];
}

for (const size of [16, 32, 48, 128]) {
    assert.deepEqual(dimensions(render(size)), [size, size]);
    assert.deepEqual(render(size), render(size));
}

const archive = childProcess.execFileSync("bash", [path.join(root, "browser/package.sh")], {
    cwd: root,
    encoding: "utf8"
}).trim();
const entries = childProcess.execFileSync("unzip", ["-Z1", archive], {encoding: "utf8"})
    .trim().split("\n").sort();
assert.deepEqual(entries, [
    "background.js",
    "connector.js",
    "icons/",
    "icons/icon-128.png",
    "icons/icon-16.png",
    "icons/icon-32.png",
    "icons/icon-48.png",
    "manifest.json",
    "samples.js",
    "submission.js"
]);

const manifest = JSON.parse(childProcess.execFileSync(
    "unzip", ["-p", archive, "manifest.json"], {encoding: "utf8"}
));
for (const size of [16, 32, 48, 128]) {
    const name = `icons/icon-${size}.png`;
    assert.equal(manifest.icons[size], name);
    const png = childProcess.execFileSync("unzip", ["-p", archive, name]);
    assert.deepEqual(dimensions(png), [size, size]);
}

console.log("browser package tests passed");
