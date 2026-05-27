Roadmap
*******

Short-term work:

* keep helper names and file contracts stable,
* keep manual pages aligned with the implemented scripts,
* make failed stress inputs easy to promote into durable tests,
* keep fetched Codeforces data isolated under ``.build/cf/``, and
* keep ``bundle`` output easy to inspect before submission.

Longer-term work:

* improve contest bootstrap from public metadata,
* make sample import failures more explicit,
* add stronger checks for stale generated submissions,
* keep the Sphinx site high level while manpages stay command-specific, and
* avoid hiding workspace state behind non-file-backed indexes.
