# JavaScript networking

microFX networking is owned by the active JavaScript application. HTTP and
socket progress is polled without blocking from the renderer loop; there is no
shell command, helper process, daemon, or worker thread.

## HTTP client

`fetch()` and `fx.net.fetch()` currently implement bounded HTTP(S) GET. They
follow at most five redirects, time out after 20 seconds, verify HTTPS against
the system CA bundle, allow four concurrent requests, and reject bodies larger
than 256 KiB.

```js
fetch("https://example.com/data.json")
  .then(response => {
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  })
  .then(data => status.text(data.message))
  .catch(error => status.text(error.message));
```

Responses expose `ok`, `status`, `url`, `text()`, and `json()`.

## UDP

```js
const socket = fx.net.udp.open({ port: 9000 });
socket.onMessage((data, peer) => {
  const message = fx.net.decode(data);
  socket.send(`received ${message}`, peer.address, peer.port);
});
socket.send(fx.net.encode("hello"), "192.168.1.20", 9001);
```

## TCP

```js
const client = fx.net.tcp.connect({ host: "example.local", port: 7000 });
client.onConnect(() => client.send("hello"));
client.onData(data => status.text(fx.net.decode(data)));
client.onError(error => status.text(error));

const server = fx.net.tcp.listen({ port: 7000 });
server.onConnection(connection => {
  connection.onData(data => connection.send(data));
});
```

Socket data is delivered as `Uint8Array`. `send()` accepts strings,
`ArrayBuffer`, and typed arrays. A runtime supports up to sixteen open sockets.

## JavaScript HTTP server

The HTTP server is JavaScript built on the public TCP API:

```js
fx.net.http.serve({ port: 8080 }, request => ({
  status: 200,
  headers: { "content-type": "application/json" },
  body: JSON.stringify({ method: request.method, path: request.path })
}));
```

The helper accepts one bounded request per connection, supports request bodies
up to 256 KiB, and closes the connection after its response. More specialized
protocol behavior can be implemented directly with `fx.net.tcp.listen()`.
