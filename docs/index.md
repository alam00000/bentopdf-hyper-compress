---
layout: home

hero:
  name: Hyper Compress
  text: The Best Open Source Compression Engine
  tagline: A high fidelity, content preserving compression engine that preserve PDF conformance, and runs everywhere from the browser to your server.
  image:
    src: /images/logo.svg
    alt: Hyper Compress
  actions:
    - theme: brand
      text: Get Started
      link: /getting-started
    - theme: alt
      text: Compress a PDF now
      link: https://hyper.bentopdf.com
    - theme: alt
      text: View on GitHub
      link: https://github.com/alam00000/bentopdf-hyper-compress

features:
  - title: Monotonic by design
    details: Hyper never makes your PDFs bigger. If there’s nothing to gain from compression, it simply gives you the original file back
  - title: Content preserving
    details: Your text stays searchable, signatures remain valid, and PDF/A files stay conformant. True lossless mode compresses your PDF without changing its content..
  - title: Reliability you can measure
    details: Tested across 27,352 runs against Ghostscript, MuPDF and qpdf, with the lowest corruption rate of any engine we measured.
  - title: Built to run everywhere
    details: The same Hyper engine powers the CLI, Node SDK, browser, self-hosted service, and C API.
---

## What is Hyper Compress

Hyper Compress is built by the [BentoPDF](https://www.bentopdf.com) team. It works throughout the entire PDF, finding the best way to reduce size without compromising fidelity.

Hyper optimizes images, fonts, content and duplicate data, choosing the best compression method for each part of the document. Every change is checked, and anything that could affect the result is automatically rolled back.

On our benchmark suite, the median document shrinks by 58% at the default preset while remaining visually identical. The full methodology and per-file results are available in the benchmarks.

## How do you want to use Hyper?

| You want to | Use |
|---|---|
| Compress a PDF right now, in the browser | [Try our live website](https://hyper.bentopdf.com) |
| Compress PDFs from the terminal | the [CLI](/cli) |
| Compress PDFs from Node.js | the [Node SDK](/node-sdk) |
| Compress PDFs in the browser | the [WebAssembly package](/wasm) |
| Run a compression service for your team | the [self-hosted Docker image](/self-hosting) |
| Embed the engine in another language | the [C API](/c-api) |
