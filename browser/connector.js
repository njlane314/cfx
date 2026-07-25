(function(root, factory) {
  "use strict";

  const commonJs = typeof module === "object" && module.exports;
  const api = factory(
    commonJs ? require("./samples.js") : root.CfxSamples,
    commonJs ? require("./submission.js") : root.CfxSubmission
  );
  if (commonJs) {
    module.exports = api;
  } else {
    api.createConnector(root).launch();
  }
})(globalThis, (samples, submission) => {
  "use strict";

  const apiInterval = 2100;

  function parseRequest(rawFragment) {
    const parameters = new URLSearchParams(rawFragment);
    const action = parameters.get("cfx");
    const port = parameters.get("port") || "";
    const token = parameters.get("token") || "";
    if (action !== "fetch" && action !== "submit") throw new Error("unknown connector action");
    if (!/^[0-9]{1,5}$/.test(port) || Number(port) < 1 || Number(port) > 65535) {
      throw new Error("invalid loopback port");
    }
    if (!/^[0-9a-f]{64}$/i.test(token)) throw new Error("invalid connector token");
    return {action, port: Number(port), token: token.toLowerCase()};
  }

  function createConnector(environment) {
    const {chrome, document, location, history, Event: EventClass} = environment;
    const fetch = environment.fetch.bind(environment);
    const now = environment.now || (() => Date.now());
    const delay = environment.delay || (milliseconds =>
      new Promise(resolve => environment.setTimeout(resolve, milliseconds)));
    const domReady = new Promise(resolve => {
      if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", resolve, {once: true});
      } else {
        resolve();
      }
    });

    function messageOf(error) {
      const message = error instanceof Error ? error.message : String(error);
      return message.replace(/\s+/g, " ").trim().slice(0, 1000) || "unknown connector error";
    }

    function cleanPageUrl() {
      const url = new URL(location.href);
      url.hash = "";
      return url.href;
    }

    function showStatus(message, failed = false) {
      let box = document.getElementById("cfx-connector-status");
      if (!box) {
        box = document.createElement("div");
        box.id = "cfx-connector-status";
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
      box.textContent = `cfx: ${message}`;
    }

    async function localRequest(request, route, options = {}) {
      const method = options.method || "GET";
      const result = await chrome.runtime.sendMessage({
        type: "cfx-local-request",
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
        body: JSON.stringify(value)
      });
      if (!response.ok) throw new Error(`local workbench returned HTTP ${response.status}`);
    }

    async function reportQuietly(request, route, value) {
      try {
        await postLocal(request, route, value);
        return true;
      } catch {
        return false;
      }
    }

    async function stateRequest(action, operation = "", value) {
      const result = await chrome.runtime.sendMessage({
        type: "cfx-submission-state",
        action,
        ...(operation ? {operation} : {}),
        ...(value ? {value} : {})
      });
      if (!result || result.ok !== true) {
        throw new Error(result?.error || "Chrome connector state is unavailable");
      }
      return result.value;
    }

    async function handleFetch(request) {
      try {
        const packageValue = samples.extractProblem(document, location);
        await postLocal(request, "fetch", packageValue);
        showStatus(`${packageValue.tests.length} sample pair(s) sent`);
      } catch (error) {
        const message = messageOf(error);
        await reportQuietly(request, "fetch-error", {message});
        showStatus(message, true);
      }
    }

    async function handleSubmit(request) {
      let saved = false;
      try {
        const artifactResponse = await localRequest(request, "submission");
        if (!artifactResponse.ok) {
          if (artifactResponse.status === 409) {
            showStatus("another connector instance is handling this submission");
            return;
          }
          throw new Error(`local workbench returned HTTP ${artifactResponse.status}`);
        }
        const artifact = submission.validateArtifact(await artifactResponse.json());
        const {form, submitter} = submission.prepareForm(
          artifact, document, location, EventClass
        );
        await submission.waitForChallenge(form, delay, now);

        const handle = submission.loggedInHandle(document, location.origin);
        const records = await submission.recentSubmissions(
          fetch, artifact.target, handle, location.origin
        );
        const lastApiRequestMillis = now();
        const pending = {
          port: request.port,
          target: artifact.target,
          handle,
          previousIds: submission.submissionIds(records),
          submittedAtMillis: now(),
          lastApiRequestMillis
        };
        await stateRequest("save", request.token, pending);
        saved = true;
        showStatus(`submitting ${artifact.target}`);
        if (submitter) form.requestSubmit(submitter);
        else form.requestSubmit();
      } catch (error) {
        if (saved) {
          try {
            await stateRequest("remove", request.token);
          } catch {
            // The original submission error is more useful.
          }
        }
        const result = {ok: false, unknown: false, message: messageOf(error)};
        await reportQuietly(request, "result", result);
        showStatus(result.message, true);
      }
    }

    function submissionResultPage() {
      return (
        /^\/enter\/?$/i.test(location.pathname) ||
        /^\/contest\/[0-9]+\/submit\/?$/i.test(location.pathname) ||
        /^\/problemset\/submit\/?$/i.test(location.pathname) ||
        /^\/problemset\/status\/?$/i.test(location.pathname) ||
        /^\/contest\/[0-9]+\/(?:my|status)\/?$/i.test(location.pathname) ||
        /^\/contest\/[0-9]+\/submission\/[0-9]+\/?$/i.test(location.pathname) ||
        /^\/problemset\/submission\/[0-9]+\/[0-9]+\/?$/i.test(location.pathname) ||
        /^\/submissions\/[^/]+\/?$/i.test(location.pathname)
      );
    }

    async function resumePendingSubmission() {
      if (!submissionResultPage()) return;
      await domReady;

      let stored;
      try {
        stored = await stateRequest("load");
      } catch (error) {
        showStatus(messageOf(error), true);
        return;
      }
      if (!stored) return;

      const pending = {...stored.value, operation: stored.operation, origin: location.origin};
      const request = {action: "submit", port: pending.port, token: pending.operation};
      let result;
      if (/^\/enter\/?$/i.test(location.pathname) || submission.signedOut(document)) {
        result = {ok: false, unknown: false, message: "Codeforces login expired"};
      } else {
        const explicitError = submission.formError(document);
        if (explicitError) result = {ok: false, unknown: false, message: explicitError};
      }

      let lastMessage = "the new submission did not appear in the Codeforces API";
      let stableIdentity = null;
      const candidateIds = new Set();
      while (!result && now() < pending.expiresAtMillis) {
        const wait = pending.lastApiRequestMillis + apiInterval - now();
        if (wait > 0) await delay(Math.min(wait, pending.expiresAtMillis - now()));
        if (now() >= pending.expiresAtMillis) break;
        pending.lastApiRequestMillis = now();
        try {
          const records = await submission.recentSubmissions(
            fetch, pending.target, pending.handle, location.origin
          );
          const identity = submission.identifySubmission(records, pending, now());
          if (identity) {
            candidateIds.add(identity.submissionId);
            if (candidateIds.size > 1) {
              result = {
                ok: false,
                unknown: true,
                message: "submission identity is ambiguous; check Codeforces before trying again"
              };
            } else if (stableIdentity?.submissionId === identity.submissionId) {
              result = {
                ok: true,
                submissionId: identity.submissionId,
                handle: identity.handle,
                submittedAtMillis: identity.submittedAtMillis
              };
              pending.url = identity.url;
            } else {
              stableIdentity = identity;
              showStatus(`confirming submission ${pending.target}`);
            }
          } else {
            stableIdentity = null;
            showStatus(`locating submission ${pending.target}`);
          }
        } catch (error) {
          lastMessage = messageOf(error);
          if (/ambiguous/i.test(lastMessage)) {
            result = {ok: false, unknown: true, message: lastMessage};
          }
        }
      }
      result ||= {
        ok: false,
        unknown: true,
        message: `${lastMessage}; check Codeforces before trying again`
      };

      const reported = await reportQuietly(request, "result", result);
      try {
        await stateRequest("remove", pending.operation);
      } catch {
        // Expiry also removes the operation; reporting is already complete.
      }
      if (result.ok) {
        showStatus(
          reported ? `submitted ${pending.target} as ${result.submissionId}`
            : "submission succeeded, but the local result could not be delivered",
          !reported
        );
        if (pending.url && submission.publicUrl(cleanPageUrl(), location.origin) !== pending.url) {
          location.assign(pending.url);
        }
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
      if (!parameters.has("cfx")) return;

      const cleanUrl = new URL(location.href);
      cleanUrl.hash = "";
      cleanUrl.searchParams.delete("cfx_reload");
      history.replaceState(history.state, "", `${cleanUrl.pathname}${cleanUrl.search}`);

      let request;
      try {
        request = parseRequest(rawFragment);
        const response = await localRequest(request, "ready");
        if (!response.ok) throw new Error(`local workbench returned HTTP ${response.status}`);
      } catch (error) {
        await domReady;
        showStatus(messageOf(error), true);
        return;
      }

      await domReady;
      if (request.action === "fetch") {
        await handleFetch(request);
      } else {
        const error = submission.pageError(document, location);
        if (error) {
          await reportQuietly(request, "result", {ok: false, unknown: false, message: error});
          showStatus(error, true);
        } else {
          await handleSubmit(request);
        }
      }
    }

    function launch() {
      const parameters = new URLSearchParams(
        location.hash.startsWith("#") ? location.hash.slice(1) : ""
      );
      if (parameters.has("cfx")) void dispatchFragment();
      else void resumePendingSubmission();
    }

    return {dispatchFragment, handleSubmit, launch, resumePendingSubmission, stateRequest};
  }

  return {createConnector, parseRequest};
});
