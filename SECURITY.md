# Security

The engine parses untrusted PDF input. The native SDK and CLI run the parser in
a separate worker process with a timeout, so a crash on hostile input kills the
worker, not the host; the WebAssembly build runs in the runtime sandbox. Inputs
over 2 GB are refused, decoded images are capped at 256 megapixels, and every
buffer writer uses overflow-checked arithmetic.

Continuous fuzzing runs on Google's OSS-Fuzz engine through ClusterFuzzLite
(`.clusterfuzzlite/`, wired into CI for pull requests that touch native code).
`make fuzz` runs the same libFuzzer harness locally. Crashes found by fuzzing
are pinned as regression seeds in `fuzz/corpus/`.

## Reporting a vulnerability

Report vulnerabilities privately to contact@bentopdf.com — do not open a
public issue. Include a minimal reproducing PDF where possible. You should hear
back within a few days.

You can also use GitHub's private vulnerability reporting: the Security tab of
this repository, then "Report a vulnerability".
