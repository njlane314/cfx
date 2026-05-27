File Contracts
**************

The source tree uses predictable paths:

``solutions/<problem>.cpp``
   The editable C++20 solution source.

``tests/<problem>/case-N.in``
   A durable input case.

``tests/<problem>/case-N.out``
   The expected output for the matching input case.

``template.cpp``
   The source copied for new problems.

``include/``
   Local C++ helpers available during local builds and bundling.

``bin/``
   Shell-facing helper commands.

``man/``
   Manual pages for terminal reference.

``.build/``
   Disposable binaries, outputs, timings, stress artifacts, and API caches.

``docs/``
   Sphinx source and rendered design documentation.
