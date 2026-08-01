(function(root, factory) {
  "use strict";

  const api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.CfxSamples = api;
  }
})(globalThis, () => {
  "use strict";

  function cleanUrl(location) {
    const url = new URL(location.href);
    url.hash = "";
    if (/^(?:m3|mirror)\.codeforces\.com$/i.test(url.hostname)) {
      url.hostname = "codeforces.com";
      url.search = "";
    }
    return url.href;
  }

  function problemIndex(pathname) {
    const match =
      pathname.match(/^\/contest\/[0-9]+\/problem\/([^/]+)/i) ||
      pathname.match(/^\/problemset\/problem\/[0-9]+\/([^/]+)/i);
    return match ? decodeURIComponent(match[1]).toUpperCase() : "";
  }

  function parseTimeLimit(text) {
    const match = text
      .replace(",", ".")
      .match(/([0-9]+(?:\.[0-9]+)?)\s*(milliseconds?|ms|seconds?|s)\b/i);
    if (!match) throw new Error("cannot read the time limit");
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
    if (!match) throw new Error("cannot read the memory limit");
    const unit = match[2].toLowerCase();
    const megabytes = Number(match[1]) *
      (unit.startsWith("g") ? 1024 : unit.startsWith("k") ? 1 / 1024 : 1);
    if (!Number.isFinite(megabytes) || megabytes <= 0 || megabytes > 2147483647) {
      throw new Error("invalid memory limit");
    }
    return Math.round(megabytes);
  }

  function renderedSample(pre) {
    const text = typeof pre.innerText === "string"
      ? pre.innerText
      : (() => {
          const copy = pre.cloneNode(true);
          for (const lineBreak of copy.querySelectorAll("br")) lineBreak.replaceWith("\n");
          return copy.textContent || "";
        })();
    return text.replace(/\r\n?/g, "\n");
  }

  function extractProblem(document, location) {
    const statement = document.querySelector(".problem-statement");
    if (!statement) throw new Error("Codeforces problem statement is unavailable");
    const title = statement.querySelector(".header .title");
    const time = statement.querySelector(".header .time-limit");
    const memory = statement.querySelector(".header .memory-limit");
    if (!title || !time || !memory) {
      throw new Error("Codeforces problem metadata is incomplete");
    }

    const index = problemIndex(location.pathname);
    let name = (title.textContent || "").replace(/\s+/g, " ").trim();
    if (index && name.toUpperCase().startsWith(`${index}.`)) {
      name = name.slice(index.length + 1).trim();
    }
    if (!name) throw new Error("Codeforces problem has no title");

    const inputs = Array.from(statement.querySelectorAll(".sample-test .input pre"));
    const outputs = Array.from(statement.querySelectorAll(".sample-test .output pre"));
    if (!inputs.length || inputs.length !== outputs.length) {
      throw new Error("Codeforces sample input/output pairs are incomplete");
    }
    return {
      name,
      url: cleanUrl(location),
      timeLimit: parseTimeLimit(time.textContent || ""),
      memoryLimit: parseMemoryLimit(memory.textContent || ""),
      tests: inputs.map((input, position) => ({
        input: renderedSample(input),
        output: renderedSample(outputs[position])
      }))
    };
  }

  return {cleanUrl, extractProblem, parseMemoryLimit, parseTimeLimit, problemIndex, renderedSample};
});
