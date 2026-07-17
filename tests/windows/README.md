# Windows feature eligibility

Windows uses the same native CTest flow as the other supported hosts. The
`feature-eligibility-policy.tsv` file contains only explicit exclusions that
cannot run as ordinary Windows/AMD64 feature tests. Every exclusion must have a
stable, test-specific reason. Eligible tests are registered directly by
`tests/CMakeLists.txt`, build with Nature's internal COFF linker, and execute the
resulting PE file on the Windows runner.
