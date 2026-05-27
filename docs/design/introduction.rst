************
Introduction
************

``cf-probs`` keeps competitive-programming work in a predictable local shape.
Each problem has one solution source under ``solutions/`` and one test directory
under ``tests/``.  Helper commands compile, run, check, bundle, stress, and
query Codeforces data without requiring per-problem project setup.

The repository is intentionally file-oriented.  Problem identities such as
``A.71`` map directly to ``solutions/A.71.cpp`` and ``tests/A.71/``.  Cached
build products and fetched Codeforces data stay under ``.build/``.

===============
Workspace Goals
===============

The workspace should:

* create a problem from a short identifier,
* keep solution and test filenames easy to predict,
* make fast reruns cheap after a first compile,
* support stricter checked builds when needed,
* bundle a single file for submission, and
* expose small commands that compose with the shell.

================
Current Surface
================

The primary entry point is the root ``Makefile``.  It forwards to scripts under
``bin/`` for work such as ``run``, ``rerun``, ``check``, ``bundle``, ``new``,
``stress``, ``probe``, ``bench``, ``case``, ``fail``, ``contest``, ``sample``,
and Codeforces metadata queries.

==============================
Guide to Reading This Document
==============================

This outline separates the design into four chapters.  The introduction states
the workspace goals.  The workspace model defines the file and identifier
contracts.  The workflow chapter describes normal problem iteration.  The
tooling chapter records the support commands and external data boundaries.
