****************
Problem Workflow
****************

The workflow keeps the common path short while preserving stronger checks for
hard cases.

=================
Creating Problems
=================

New problem files are created from the root:

.. code-block:: sh

   make new A 71

The helper creates the solution path and test directory using the repository's
normal naming rules.

=================
Running and Check
=================

Fast local runs use the default optimized build:

.. code-block:: sh

   make run A 71

Checked runs use the slower diagnostics path:

.. code-block:: sh

   make check A 71

The checked build enables stricter warnings, sanitizer support where available,
libstdc++ debug mode, and local debug flags.

=============
Cached Reruns
=============

After binaries are built, ``rerun`` can run cached products without compiling:

.. code-block:: sh

   make rerun all

This is useful when checking many stored samples after a template or helper
change.

================
Ad Hoc Iteration
================

``probe`` runs one arbitrary input without adding it to ``tests/``.  ``bench``
runs the same input repeatedly and summarizes timing.  ``case`` records a new
test case.  ``stress`` compares the solution against a brute-force checker and
leaves failing artifacts under ``.build/stress/<problem>/`` for inspection.
