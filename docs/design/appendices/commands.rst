Command Summary
***************

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Command
     - Purpose
   * - ``make new A 71``
     - Create the solution and test directory for a problem.
   * - ``make run A 71``
     - Compile and run stored cases with the fast local build.
   * - ``make check A 71``
     - Run stored cases with stricter diagnostics.
   * - ``make rerun all``
     - Reuse cached binaries without compiling.
   * - ``make bundle A 71``
     - Write a single submission source to standard output.
   * - ``make probe A 71 input.txt``
     - Run one ad hoc input without adding a test case.
   * - ``make bench A 71 input.txt N=5``
     - Run one input repeatedly and summarize timing.
   * - ``make case A 71``
     - Record the next numbered test case.
   * - ``make stress A 71 GEN=... BRUTE=...``
     - Compare the solution against a brute-force checker.
   * - ``make sample A 71``
     - Try to fetch and store public samples.
   * - ``make cc``
     - Listen for Competitive Companion sample packages.
   * - ``make man``
     - Render the manual set.
