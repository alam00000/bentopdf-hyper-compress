---
layout: home
title: Hyper Compress
description: High fidelity, content preserving PDF compression for the browser, the terminal, Node.js, Rust, C and your own servers.

hero:
  name: Hyper Compress
  text: Smaller PDFs that still work
  tagline: A PDF compression engine that keeps text searchable, signatures valid and PDF/A honest, and never returns a bigger file. Runs in the browser, the terminal, Node.js, Rust, C and Docker.
  image:
    src: /images/logo.svg
    alt: Hyper Compress
  actions:
    - theme: brand
      text: Quickstart
      link: /getting-started
    - theme: alt
      text: Compress a PDF now
      link: https://hyper.bentopdf.com
    - theme: alt
      text: View on GitHub
      link: https://github.com/alam00000/bentopdf-hyper-compress

features:
  - title: Never bigger
    details: If Hyper cannot make a file smaller, you get your original bytes back. In 4,000 benchmark runs it made no file bigger.
    link: /guarantees
    linkText: Read the guarantees
  - title: Content preserving
    details: Text stays searchable, digital signatures stay valid, tags stay for screen readers, and a lossless mode changes no pixel.
    link: /guides/choosing-settings
    linkText: Choose your settings
  - title: Measured, in public
    details: On 1,000 real PDFs it saved more than the PDF Tools, Apryse and Nutrient SDKs at every level, with no failures. Every file and script is published.
    link: /benchmarks
    linkText: See the benchmarks
  - title: Fast at scale
    details: One 8-CPU server compresses about 11,700 real PDFs an hour, or 65,000 an hour when they are under 5 MB. Every job runs in its own process, safely in parallel.
    link: /benchmarks#throughput
    linkText: See the throughput
  - title: One engine, everywhere
    details: The same engine and options in the browser, the CLI, Node.js, Rust, C and a self-hosted HTTP service, on macOS, Linux (x64 and ARM64) and Windows.
    link: /platforms
    linkText: Platforms and surfaces
---

## What Hyper does

Hyper Compress goes through every part of a PDF and stores it in less space: images, fonts, page content and duplicate data. For each image it tries several encodings and keeps the smallest one that still looks right. Every risky change is checked, and anything that would change how a page looks is rolled back.

On top of the engine, Hyper adds the parts that make it safe to run on files you did not make: it decrypts with your password, repacks the file, can search for a target size, keeps PDF/A files honest, never returns a bigger file, and tells you about anything it skipped.

## Start here

| You want to | Go to |
|---|---|
| Compress your first PDF in five minutes | [Quickstart](/getting-started) |
| Pick settings for your documents | [Choose the right settings](/guides/choosing-settings) |
| Get every file under an upload limit | [Hit a file size target](/guides/size-targets) |
| Shrink scanned documents | [Scanned documents](/guides/scanned-documents) |
| Run a compression service for your team | [Self-hosting](/self-hosting) and the [HTTP API](/http-api) |
| Look up a function, flag, header or option | [CLI](/cli), [Node.js SDK](/node-sdk), [HTTP API](/http-api), [Options](/options) |

## How a compression runs

1. **Read.** Hyper opens the PDF. If it is encrypted, it decrypts it with the password you gave.
2. **Compress.** The engine runs in its own process, so a broken or hostile file cannot crash your program. It re-encodes images, subsets and merges fonts, rewrites page content compactly and removes what you asked it to remove.
3. **Repack.** Objects are packed into compressed streams.
4. **Search** (only with a size target). Hyper tries lower quality and then lower resolution until the file fits, or returns the smallest file it can make.
5. **Check.** If the result is not smaller than the input, you get the input back. PDF/A claims are kept only when they are still true.
6. **Report.** You get the sizes, whether the file was signed, its PDF/A level, whether a target was met, and a warning for anything that was skipped.

## Key ideas

- **Presets** are complete sets of options: `low`, `medium` (the default), `high` and `lossless`. Start from one and change what you need. See [Options](/options).
- **Outcomes are not errors.** "No gain", "signed, returned untouched" and "target not met" are normal results with clear flags. Errors are only for things like a wrong password or a timeout. See [Errors](/errors).
- **Warnings explain every skip.** When an option is unsafe for a file, Hyper skips it and tells you why. See [Warnings](/warnings).
