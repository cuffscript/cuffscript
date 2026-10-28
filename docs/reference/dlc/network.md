# `DLC:network`

```cuff
use DLC:network
```

A small client for plain HTTP requests. There is no TLS: an `https://` address is refused with a clear error instead of
being silently downgraded.

## Functions

| Function | Result |
| :--- | :--- |
| `network_get(url)` | Makes a GET request. |
| `network_post(url, body [, content_type])` | Makes a POST request with the given body. The content type defaults to `text/plain; charset=utf-8`. |

The URL, the body and the content type must be strings (`E4-005` otherwise). Both functions return a map
`{"status": number, "ok": boolean, "body": str}`, where `ok` is true for statuses 200 to 299. A response that is an HTTP
error, such as `404`, is **not** an error in CuffScript: you get `ok` as `false` and the body the server sent. Only a request
that could not be completed is an error.

```cuff
use DLC:network
set map res to network_get("http://example.com/")
if res["ok"] do:
    print(res["status"])
end
```

## What is sent

The request is HTTP/1.1 with `Host`, `Accept: */*`, a `User-Agent` that names CuffScript, and `Connection: close`; `network_post` adds
`Content-Type` and `Content-Length`. There is no way to add headers, cookies or authentication, and no way to read response
headers; a script that needs them has to use another tool.

The body of the response is returned as text. A response sent in chunks is reassembled, and a response with no body
(for example `204`) gives an empty string.

## Redirects

`301`, `302`, `303`, `307` and `308` responses that carry a `Location` header are followed, up to the redirect limit in [limits](../limits.md).

- A relative `Location` is resolved against the current host.
- `303`, and `301` or `302` after a `POST`, turn the next request into a `GET` and drop the body; `307` and `308` repeat the request unchanged, with the same method, body and content type.
- A redirect to an `https://` address, or to an address the library blocks, is an error like any request to one.
- When the answer after the last allowed redirect is still a redirect, that response is returned as it is, with its `3xx` status and `ok` set to `false`; it is not an error.

## Errors

- `url` must start with `http://`. Any other scheme is `E4-028`, and `https://` gets a message that says so.
- A connection failure, a timeout, a response over the size limit, a malformed response, or a blocked address is `E4-028`,
  which `or_else` can catch. The time and size limits are in [limits](../limits.md).
- By default the client cannot reach loopback, private or link-local addresses, which protects a host from
  server-side request forgery. `--allow-private-network` lifts that, which is what lets a script talk to a server on the same machine.

```cuff
use DLC:network
set map res to network_get("http://example.com/") or_else do:
    print("could not reach the server")
    change res to {"status": 0, "ok": false, "body": ""}
end
print(res["ok"])
```

## When the library is switched off

A host can switch the library off with `--no-network`; every call then raises `E5-005`, as described for
[all libraries](README.md#libraries-that-reach-outside-the-program).

A script can still make many requests in a loop. A host that runs untrusted scripts should set `--max-steps`
and `--timeout` as well; the [security policy](../../../SECURITY.md) explains the whole picture.
