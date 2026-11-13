---
title: HTTP API
description: Compress PDFs and check passwords over HTTP with the self-hosted Hyper Compress service, with every endpoint, parameter, header and error.
---

# HTTP API

The Hyper Compress service takes a PDF in the request body and returns the compressed PDF in the response body. It runs on your own server, so your files never leave it.

Start the service with Docker:

```bash
docker run -d --name hyper -p 8080:8080 \
  ghcr.io/alam00000/bentopdf-hyper-compress:latest
```

[Self-hosting](/self-hosting) covers configuration, TLS, scaling and Kubernetes.

## Base URL

Every path on this page is relative to where you run the service. The examples use:

```text
http://localhost:8080
```

The service speaks plain HTTP. Put a reverse proxy in front of it for HTTPS; [Self-hosting](/self-hosting#put-tls-in-front) has Caddy and nginx examples.

## Authentication

By default the API is open to anyone who can reach the port. Set the `HYPER_API_TOKEN` environment variable and every request to `/api/compress` and `/api/verify-password` must carry that token in one of these headers:

| Header | Example |
|---|---|
| `Authorization` | `Authorization: Bearer my-secret-token` |
| `x-api-token` | `x-api-token: my-secret-token` |

When both are sent, a `Bearer` value in `Authorization` wins.

::: code-group

```bash [Bearer]
curl -o output.pdf --data-binary @input.pdf \
  -H 'Authorization: Bearer my-secret-token' \
  'http://localhost:8080/api/compress'
```

```bash [x-api-token]
curl -o output.pdf --data-binary @input.pdf \
  -H 'x-api-token: my-secret-token' \
  'http://localhost:8080/api/compress'
```

:::

A missing or wrong token returns `401`:

```json
{"error":"unauthorized"}
```

What stays open when a token is set:

- `GET /healthz`, so load balancers and probes need no token.
- `GET /` and the files under `/web/` and `/dist/`, so the web page still loads.

::: warning
The web page cannot send a token. With `HYPER_API_TOKEN` set, the page loads but every compression from it fails with `401`. To protect a page that people use in the browser, leave the token unset and put a login in your reverse proxy instead.
:::

## Endpoints

| Method and path | What it does | Token |
|---|---|---|
| [`POST /api/compress`](#post-api-compress) | Compresses the PDF in the body and returns the result. | required when set |
| [`POST /api/verify-password`](#post-api-verify-password) | Checks a password against an encrypted PDF without compressing it. | required when set |
| [`GET /healthz`](#get-healthz) | Reports that the service is up and how busy it is. | never |
| [`GET /`](#get-and-static-files) | Serves the web page. | never |
| [`GET /web/...`, `GET /dist/...`](#get-and-static-files) | Serves the web page's scripts, styles and fonts. | never |

Any other method or path returns `404` with `{"error":"not_found"}`. That includes `GET /api/compress`: the API only takes `POST`.

## POST /api/compress

Compresses one PDF. Send the PDF bytes as the raw request body, with any `Content-Type`. Choose settings with query parameters.

```http
POST /api/compress?preset=high HTTP/1.1
Host: localhost:8080
Content-Type: application/pdf

%PDF-1.7 ...
```

### Query parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `preset` | string | `medium` | `low`, `medium`, `high`, `lossless` or `custom`. Case-sensitive. `custom` starts from `medium` and requires `options`. |
| `options` | JSON object | none | [Options](/options) applied on top of the preset. At most 4096 UTF-16 code units after URL decoding. |
| `targetSizeBytes` | number | none | Target size in bytes, greater than zero. Decimals are rounded down. Hyper searches for settings that fit; see [Hit a file size target](/guides/size-targets). |
| `brotli` | `true` | not set | `brotli=true` turns on PDF 2.0 Brotli stream compression. Same as `"brotli": true` in `options`. |
| `preservePdfa` | `true` | not set | `preservePdfa=true` keeps a PDF/A input (the archiving version of PDF) conformant. Same as `"preserveConformance": true` in `options`. See [PDF/A archives](/guides/pdfa). |

How the parameters combine:

- `brotli=true` and `preservePdfa=true` override the same fields in `options`. Any other value, such as `1` or `yes`, counts as not set, and then the value from `options` (or the preset) applies.
- Fields in `options` that are not option names are ignored.
- Numbers outside an option's range are clamped to the range. A value of the wrong type, such as `"50"` instead of `50`, falls back to that option's default. [Options](/options) lists every range and default.
- An empty value, such as `targetSizeBytes=`, counts as not set.
- If a parameter appears twice, the first one wins. Unknown parameters are ignored.

### Request headers

| Name | Required | Description |
|---|---|---|
| `X-Password` | no | Password for an encrypted PDF, as plain ASCII. Only the first line is used. See [Encrypted PDFs](/guides/encrypted-pdfs). |
| `X-Password-Encoded` | no | The same password as UTF-8, percent-encoded like JavaScript's `encodeURIComponent`. Use it for any password with non-ASCII characters. A literal `+` stays `+`. Wins over `X-Password` when both are sent. |
| `Authorization` or `x-api-token` | when `HYPER_API_TOKEN` is set | The API token. See [Authentication](#authentication). |

### Response

On success the status is `200` and the body is the PDF.

| Header | Example | Description |
|---|---|---|
| `Content-Type` | `application/pdf` | Always `application/pdf` on success. |
| `X-Original-Size` | `2593056` | Input size in bytes. |
| `X-Compressed-Size` | `915051` | Output size in bytes. Never larger than `X-Original-Size`. |
| `X-Signed` | `false` | `true` when the input was digitally signed and returned untouched. See [Signed documents](/guides/signed-documents). |
| `X-Signatures-Removed` | `false` | `true` when `removeSignatures` removed the signatures and the compressed file was returned. |
| `X-Pdfa` | `2b` | The PDF/A part and level the input claims, or empty. |
| `X-Pdfa-Preserved` | `false` | `true` when the output still claims that PDF/A level. |
| `X-Met-Target` | `true` | `true` or `false` when you sent `targetSizeBytes`, empty otherwise. |
| `X-Warnings` | `%5B%22...%22%5D` | URL-encoded JSON array of [warnings](/warnings), or empty when there are none. See [Decode X-Warnings](#decode-x-warnings). |

When Hyper cannot make the file smaller, the response is your original bytes and the two size headers are equal. That is a result, not an error.

### Errors

Errors return a JSON body with an `error` code. Some add extra fields.

| Status | `error` | Extra fields | When |
|---|---|---|---|
| `400` | `bad_preset` | `presets`: the valid names | `preset` is not one of the five names. |
| `400` | `bad_target_size` | | `targetSizeBytes` is not a number greater than zero. |
| `400` | `bad_options` | | `options` is not valid JSON, is not an object, or is over 4096 UTF-16 code units. |
| `400` | `options_required` | | `preset=custom` without `options`. |
| `400` | `not_a_pdf` | | The body is shorter than 5 bytes or does not start with `%PDF`. |
| `400` | `decrypt_failed` | | The PDF needs a password to open and none was sent, or the password is wrong. |
| `401` | `unauthorized` | | `HYPER_API_TOKEN` is set and the token is missing or wrong. |
| `413` | `too_large` | `maxBytes`: the limit in bytes | The body is larger than `HYPER_MAX_UPLOAD_MB`. |
| `429` | `busy` | `detail`: `"queue full, retry later"` | All slots are busy and the queue is full. The response has a `Retry-After: 1` header. |
| `500` | `engine_error` | | The engine could not process the document. |
| `500` | `cancelled` | | The job was stopped. The service does not stop jobs today, so you should not see this code. |
| `500` | `internal` | | The server could not create, write or read its temporary files. |
| `504` | `timeout` | | The job, or one step of it, ran longer than `HYPER_TIMEOUT_MS`. |

Example bodies:

```json
{"error":"bad_preset","presets":["low","medium","high","lossless","custom"]}
```

```json
{"error":"too_large","maxBytes":524288000}
```

```json
{"detail":"queue full, retry later","error":"busy"}
```

Field order in error bodies is not fixed; parse them as JSON. [Errors](/errors) explains each code and what to do about it.

### Examples

Compress with the default `medium` preset:

```bash
curl -o output.pdf --data-binary @input.pdf \
  'http://localhost:8080/api/compress'
```

Use `--data-binary`. Plain `-d` strips line breaks and breaks the PDF.

Show the result headers without opening the file:

```bash
curl -s -o output.pdf -D - --data-binary @input.pdf \
  'http://localhost:8080/api/compress?preset=high' | grep -i '^x-'
```

```text
x-original-size: 2593056
x-compressed-size: 915051
x-signed: false
x-signatures-removed: false
x-pdfa:
x-pdfa-preserved: false
x-met-target:
x-warnings:
```

Send options and let curl URL-encode them with `--url-query` (curl 7.87 or newer):

```bash
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'preset=custom' \
  --url-query 'options={"imageQuality":35,"grayscale":true}' \
  'http://localhost:8080/api/compress'
```

::: warning
Do not use `-G` with `--data-binary`. `-G` moves the request body into the URL, so curl sends your PDF as a query string instead of a body.
:::

For a password with non-ASCII characters, percent-encode it as UTF-8 and send `X-Password-Encoded` instead, for example `-H 'X-Password-Encoded: p%C3%A4sswort'` for `pässwort`. Non-ASCII bytes in `X-Password` make the connection drop.

Aim for a size and send a password:

```bash
curl -o output.pdf --data-binary @input.pdf \
  -H 'X-Password: hunter2' \
  --url-query 'targetSizeBytes=2097152' \
  'http://localhost:8080/api/compress'
```

The request with custom options, in each language:

::: code-group

```bash [curl]
curl -D - -o output.pdf --data-binary @input.pdf \
  --url-query 'preset=custom' \
  --url-query 'options={"imageQuality":35,"grayscale":true}' \
  'http://localhost:8080/api/compress'
```

```js [Node.js]
import { readFile, writeFile } from 'node:fs/promises';

const params = new URLSearchParams({
  preset: 'custom',
  options: JSON.stringify({ imageQuality: 35, grayscale: true }),
});

const res = await fetch(`http://localhost:8080/api/compress?${params}`, {
  method: 'POST',
  headers: { 'Content-Type': 'application/pdf' },
  body: await readFile('input.pdf'),
});

if (!res.ok) {
  const err = await res.json();
  throw new Error(`HTTP ${res.status}: ${err.error}`);
}

await writeFile('output.pdf', Buffer.from(await res.arrayBuffer()));

const warningsHeader = res.headers.get('x-warnings');
const warnings = warningsHeader ? JSON.parse(decodeURIComponent(warningsHeader)) : [];

console.log(`${res.headers.get('x-original-size')} -> ${res.headers.get('x-compressed-size')} bytes`);
console.log(warnings);
```

```python [Python]
import json
from urllib.parse import unquote

import requests

with open("input.pdf", "rb") as f:
    res = requests.post(
        "http://localhost:8080/api/compress",
        params={
            "preset": "custom",
            "options": json.dumps({"imageQuality": 35, "grayscale": True}),
        },
        headers={"Content-Type": "application/pdf"},
        data=f,
    )

if not res.ok:
    raise RuntimeError(f"HTTP {res.status_code}: {res.json()['error']}")

with open("output.pdf", "wb") as out:
    out.write(res.content)

header = res.headers.get("x-warnings", "")
warnings = json.loads(unquote(header)) if header else []

print(f"{res.headers['x-original-size']} -> {res.headers['x-compressed-size']} bytes")
print(warnings)
```

```ruby [Ruby]
require "json"
require "net/http"
require "uri"

uri = URI("http://localhost:8080/api/compress")
uri.query = URI.encode_www_form(
  preset: "custom",
  options: JSON.generate({ imageQuality: 35, grayscale: true })
)

req = Net::HTTP::Post.new(uri, "Content-Type" => "application/pdf")
req.body = File.binread("input.pdf")
res = Net::HTTP.start(uri.host, uri.port, read_timeout: 600) { |http| http.request(req) }

raise "HTTP #{res.code}: #{JSON.parse(res.body)['error']}" unless res.is_a?(Net::HTTPSuccess)

File.binwrite("output.pdf", res.body)

header = res["x-warnings"].to_s
warnings = header.empty? ? [] : JSON.parse(URI::DEFAULT_PARSER.unescape(header))

puts "#{res['x-original-size']} -> #{res['x-compressed-size']} bytes"
p warnings
```

```go [Go]
package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
)

func main() {
	input, err := os.ReadFile("input.pdf")
	if err != nil {
		panic(err)
	}
	options, _ := json.Marshal(map[string]any{"imageQuality": 35, "grayscale": true})
	query := url.Values{"preset": {"custom"}, "options": {string(options)}}

	res, err := http.Post("http://localhost:8080/api/compress?"+query.Encode(), "application/pdf", bytes.NewReader(input))
	if err != nil {
		panic(err)
	}
	defer res.Body.Close()
	body, err := io.ReadAll(res.Body)
	if err != nil {
		panic(err)
	}

	if res.StatusCode != http.StatusOK {
		var e struct{ Error string }
		json.Unmarshal(body, &e)
		panic(fmt.Sprintf("HTTP %d: %s", res.StatusCode, e.Error))
	}

	if err := os.WriteFile("output.pdf", body, 0o644); err != nil {
		panic(err)
	}

	warnings := []string{}
	if h := res.Header.Get("X-Warnings"); h != "" {
		decoded, _ := url.PathUnescape(h)
		json.Unmarshal([]byte(decoded), &warnings)
	}

	fmt.Printf("%s -> %s bytes\n", res.Header.Get("X-Original-Size"), res.Header.Get("X-Compressed-Size"))
	fmt.Println(warnings)
}
```

```java [Java]
import java.net.URI;
import java.net.URLDecoder;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;

import static java.nio.charset.StandardCharsets.UTF_8;

public class Compress {
    public static void main(String[] args) throws Exception {
        String options = "{\"imageQuality\":35,\"grayscale\":true}";
        URI uri = URI.create("http://localhost:8080/api/compress?preset=custom&options="
                + URLEncoder.encode(options, UTF_8));

        HttpRequest request = HttpRequest.newBuilder(uri)
                .header("Content-Type", "application/pdf")
                .POST(HttpRequest.BodyPublishers.ofFile(Path.of("input.pdf")))
                .build();
        HttpClient client = HttpClient.newBuilder().version(HttpClient.Version.HTTP_1_1).build();
        HttpResponse<byte[]> res = client.send(request, HttpResponse.BodyHandlers.ofByteArray());

        if (res.statusCode() != 200) {
            throw new RuntimeException("HTTP " + res.statusCode() + ": " + new String(res.body(), UTF_8));
        }

        Files.write(Path.of("output.pdf"), res.body());

        String header = res.headers().firstValue("x-warnings").orElse("");
        String warnings = header.isEmpty() ? "[]" : URLDecoder.decode(header, UTF_8);

        System.out.println(res.headers().firstValue("x-original-size").orElse("") + " -> "
                + res.headers().firstValue("x-compressed-size").orElse("") + " bytes");
        System.out.println(warnings);
    }
}
```

```php [PHP]
<?php
$query = http_build_query([
    'preset' => 'custom',
    'options' => json_encode(['imageQuality' => 35, 'grayscale' => true]),
]);

$headers = [];
$ch = curl_init("http://localhost:8080/api/compress?$query");
curl_setopt_array($ch, [
    CURLOPT_POST => true,
    CURLOPT_POSTFIELDS => file_get_contents('input.pdf'),
    CURLOPT_HTTPHEADER => ['Content-Type: application/pdf'],
    CURLOPT_RETURNTRANSFER => true,
    CURLOPT_HEADERFUNCTION => function ($ch, $line) use (&$headers) {
        $parts = explode(':', $line, 2);
        if (count($parts) === 2) {
            $headers[strtolower(trim($parts[0]))] = trim($parts[1]);
        }
        return strlen($line);
    },
]);
$body = curl_exec($ch);
$status = curl_getinfo($ch, CURLINFO_RESPONSE_CODE);

if ($status !== 200) {
    throw new RuntimeException("HTTP $status: " . json_decode($body, true)['error']);
}

file_put_contents('output.pdf', $body);

$header = $headers['x-warnings'] ?? '';
$warnings = $header === '' ? [] : json_decode(rawurldecode($header), true);

echo "{$headers['x-original-size']} -> {$headers['x-compressed-size']} bytes\n";
print_r($warnings);
```

:::

The Node.js sample uses the built-in `fetch` of Node.js 22; save it as `compress.mjs`. The Python sample needs the [`requests`](https://pypi.org/project/requests/) package (`pip install requests`). The Ruby and Go samples use only the standard library; run them with `ruby compress.rb` and `go run main.go`. The Java sample needs Java 11 or newer; save it as `Compress.java` and run `java Compress.java`. It asks for HTTP/1.1, because Java's client otherwise tries to upgrade the connection to HTTP/2. The PHP sample needs the `curl` extension.

### Decode X-Warnings

`X-Warnings` is a JSON array of strings, URL-encoded so it fits in a header. Decode it in two steps: URL-decode, then parse the JSON. An empty header means no warnings.

| Language | Code |
|---|---|
| JavaScript | `h ? JSON.parse(decodeURIComponent(h)) : []` |
| Python | `json.loads(urllib.parse.unquote(h)) if h else []` |
| Ruby | `h.empty? ? [] : JSON.parse(URI::DEFAULT_PARSER.unescape(h))` |
| Go | `s, _ := url.PathUnescape(h); json.Unmarshal([]byte(s), &warnings)` |
| Java | `h.isEmpty() ? "[]" : URLDecoder.decode(h, UTF_8)`, then parse with your JSON library |
| PHP | `$h === '' ? [] : json_decode(rawurldecode($h), true)` |

A missed size target, for example, gives:

```text
x-met-target: false
x-warnings: %5B%22target%20of%201000%20bytes%20not%20reached%3A%20999426%20bytes%20is%20the%20smallest%20this%20document%20compresses%20to%20at%20quality%205%20and%2036%20dpi.%20For%20image-only%20documents%2C%20rasterizePages%20can%20go%20further.%22%5D
```

which decodes to:

```json
["target of 1000 bytes not reached: 999426 bytes is the smallest this document compresses to at quality 5 and 36 dpi. For image-only documents, rasterizePages can go further."]
```

### How a request is handled

1. The service checks the token, then the query parameters. Problems here return `401` or `400` right away, before any upload is read.
2. The request waits for a free slot. Up to `HYPER_CONCURRENCY` jobs run at once and up to `HYPER_QUEUE` more wait in order. When the queue is full the request gets `429` at once, with `Retry-After: 1`.
3. Only when a slot is free does the service read the body. A waiting client's upload stays paused until then.
4. A body over `HYPER_MAX_UPLOAD_MB` gets `413` as soon as the limit is passed. The service stops reading, so some clients report a closed connection instead of the `413` body.
5. The PDF is written to a fresh temporary folder, compressed, sent back and deleted.

`HYPER_TIMEOUT_MS` limits the whole job and each step inside it. When a client disconnects, the job still runs to the end; the result is discarded.

An encrypted PDF that needs a password to open, sent without one, gets `400` `decrypt_failed`. A PDF with only an owner password (one that limits printing or editing but opens without a password) compresses without one, and the output keeps that owner password.

## POST /api/verify-password

Checks whether a password opens an encrypted PDF, without compressing it. Use it to check a user's password before you queue real work.

Send the PDF as the body and the password in `X-Password`, or in `X-Password-Encoded` when it has non-ASCII characters.

### Request headers

| Name | Required | Description |
|---|---|---|
| `X-Password` | no | The password to check, as plain ASCII. Missing means an empty password. |
| `X-Password-Encoded` | no | The password as percent-encoded UTF-8. Wins over `X-Password`. |
| `Authorization` or `x-api-token` | when `HYPER_API_TOKEN` is set | The API token. |

### Response

Status `200` with a JSON body:

| Field | Type | Description |
|---|---|---|
| `valid` | boolean | `true` when `/api/compress` would accept the same password for this file. |

```bash
curl -s --data-binary @input.pdf -H 'X-Password: hunter2' \
  'http://localhost:8080/api/verify-password'
```

```json
{"valid":true}
```

What to expect:

- An unencrypted PDF returns `{"valid":true}` for any password.
- A body that is not a PDF returns `{"valid":false}`. This endpoint does not check for `%PDF`.
- The check has its own 60 second limit; `HYPER_TIMEOUT_MS` does not apply. A check that runs out of time returns `500` `internal`.

### Errors

It shares the token, the upload limit and the queue with `/api/compress`:

| Status | `error` | When |
|---|---|---|
| `401` | `unauthorized` | The token is missing or wrong. |
| `413` | `too_large` | The body is larger than `HYPER_MAX_UPLOAD_MB`. The body includes `maxBytes`. |
| `429` | `busy` | The queue is full. The body includes `detail`, and the response has `Retry-After: 1`. |
| `500` | `internal` | The check could not run: it passed 60 seconds, the server could not write its temporary file, or it cannot find `qpdf`. |

## GET /healthz

Reports that the service is up and how busy it is. It never needs a token and never waits in the queue.

```bash
curl -s 'http://localhost:8080/healthz'
```

```json
{"active":0,"ok":true,"queued":0}
```

| Field | Type | Description |
|---|---|---|
| `ok` | boolean | Always `true` when the service answers. |
| `active` | integer | Jobs running now, at most `HYPER_CONCURRENCY`. |
| `queued` | integer | Jobs waiting for a slot, at most `HYPER_QUEUE`. |

`queued` close to `HYPER_QUEUE` for long stretches means you need more concurrency or more instances. [Self-hosting](/self-hosting#health-checks) shows how to wire this into Docker Compose and Kubernetes.

## GET / and static files

`GET /` serves the same page as [hyper.bentopdf.com](https://hyper.bentopdf.com), changed to send files to this service's API instead of compressing them in the browser. Files are compressed on the server.

The page loads its scripts, styles and fonts from two folders:

| Path | Serves |
|---|---|
| `/web/...` | the page's own scripts, styles, fonts, icons and images |
| `/dist/...` | the preset and option definitions the page shares with the Node.js SDK |

Only names ending in `.js`, `.css`, `.woff2`, `.svg` or `.png` are served, and only when the path before the extension contains letters, digits, `/`, `_` and `-`. Anything else, or a missing file, returns `404` with `{"error":"not_found"}`.

If the service cannot find its web files, `GET /` returns a short placeholder page and the API keeps working. [Self-hosting](/self-hosting#environment-variables) explains `HYPER_ROOT`, which points the service at them.

## Limits

| Limit | Default | Set with |
|---|---|---|
| Upload size | 500 MB | `HYPER_MAX_UPLOAD_MB` |
| Jobs at once | 2 | `HYPER_CONCURRENCY` |
| Jobs waiting | 8 | `HYPER_QUEUE` |
| Time per job | 10 minutes | `HYPER_TIMEOUT_MS` |
| `options` length | 4096 UTF-16 code units | fixed |
| Password check time | 60 seconds | fixed |

[Self-hosting](/self-hosting#environment-variables) lists every setting with its allowed range.

## Next steps

- [Self-hosting](/self-hosting): configure, secure and scale the service.
- [Options](/options): every setting you can pass in `options`.
- [Errors](/errors): what each error code means and how to handle it.
- [Running in production](/guides/production): batches, retries and untrusted uploads.
