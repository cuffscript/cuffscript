# `DLC:network`

```cuff
use DLC:network
```

A small client for plain HTTP requests. There is no TLS: an `https://` address is refused with a clear error
instead of being silently downgraded.

| Function | Result |
| :--- | :--- |
| `network_get(url)` | Makes a GET request. |
| `network_post(url, body [, content_type])` | Makes a POST request with the given body. |

Both return a map `{"status": number, "ok": boolean, "body": str}`, where `ok` is true for statuses 200 to 299.

```cuff
use DLC:network
set res to network_get("http://example.com/")
if res["ok"] do:
    print(length(res["body"]))
end
```

- `url` must start with `http://`.
- A connection failure, a timeout, a response over the size limit, or a blocked address is `E4-028`, which
  `or_else` can catch. The time and size limits are in [limits](../limits.md).
- By default the client cannot reach loopback, private or link-local addresses, which protects a host from
  server-side request forgery. `--allow-private-network` lifts that, and `--no-network` switches the library off
  (then `use DLC:network` is `E5-005`).

A script can still make many requests in a loop. A host that runs untrusted scripts should set `--max-steps`
and `--timeout` as well; the [security policy](../../../SECURITY.md) explains the whole picture.
