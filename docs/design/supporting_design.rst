Tooling and Integration
***********************

The supporting tools keep contest setup, sample import, and submission output
separate from the core compile-run loop.

============
Script Shape
============

The root ``Makefile`` is a dispatcher.  The executable behavior lives in small
scripts under ``bin/`` so commands can also be run directly when useful.

The helper layer should preserve simple inputs and visible file outputs instead
of hiding state in a database.

================
Codeforces Data
================

API-backed helpers cache fetched data under ``.build/cf/``.  They support
queries such as problem metadata, accepted submissions for a handle, local
solution coverage, rating history, public contest setup, and upcoming contests.

=============
Sample Import
=============

``sample`` tries public online sample sources.  The official Codeforces API does
not expose samples, so ``cc`` can listen for Competitive Companion packages on
port 27121 and write received cases into the local test layout.

=================
Submission Output
=================

``bundle`` writes a single source file to standard output.  This keeps the
submission step explicit and makes it easy to inspect exactly what will be sent
to Codeforces.

===================
Generated Artifacts
===================

Generated binaries, timings, normalized outputs, API responses, stress failures,
and fetched samples belong under ``.build/`` or ``tests/`` depending on whether
they are disposable cache or durable test input.
