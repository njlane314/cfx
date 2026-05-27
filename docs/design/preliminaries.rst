***************
Workspace Model
***************

The design uses a small vocabulary so that helpers, tests, and generated files
can agree on paths.

===================
Problem Identifiers
===================

A *problem* is named by a letter and contest number, for example ``A.71``.  The
same identifier is accepted by the make targets and helper scripts.  Some
commands also accept compact Codeforces spellings such as ``71A`` where that is
more natural for API lookup.

=========
Solutions
=========

A solution source lives at ``solutions/<problem>.cpp``.  New files start from
``template.cpp`` and may include local helpers from ``include/`` while running
locally.  ``bundle`` emits a submission-ready single source.

=====
Tests
=====

Sample and local tests live under ``tests/<problem>/`` as ``case-N.in`` and
``case-N.out``.  The test naming is stable so failed inputs can be promoted from
scratch runs into checked cases.

===========
Build Cache
===========

Compiled binaries, normalized outputs, timing files, stress failures, and
Codeforces API caches belong under ``.build/``.  The cache is disposable and is
not part of the source contract.

-------
Manuals
-------

Manual pages under ``man/`` are the terminal reference.  The Sphinx site gives a
higher-level outline and should avoid duplicating every manpage detail.
