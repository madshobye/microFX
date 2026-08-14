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

Responses expose `ok`, `status`, `url`, `text()`, `json()`, and
`arrayBuffer()`. The binary response is still subject to the same 256 KiB
limit.

## WebSocket client

`fx.net.websocket.connect()` supports non-blocking `ws://` and TLS-verified
`wss://` streams. A runtime supports four connections, complete messages are
bounded to 256 KiB, and one outgoing message of at most 64 KiB may be queued
per connection. Incoming messages are held in a bounded native queue and at
most eight are delivered to JavaScript per frame, so a busy stream cannot
monopolize rendering.

```js
const socket = fx.net.websocket.connect("wss://stream.example.com/events");
socket.onOpen(() => socket.send(JSON.stringify({ subscribe: "positions" })));
socket.onMessage(text => update(JSON.parse(text)));
socket.onClose(() => status.text("DISCONNECTED"));
socket.onError(error => status.text(String(error)));
```

Credentials should be installed outside projects as files below
`/data/config/secrets/` and read with `fx.secret()`. For example,
`fx.secret("SERVICE_API_KEY", "")` reads
`/data/config/secrets/SERVICE_API_KEY`; secret names cannot contain path
separators.

## Cached raster maps

`fx.tileMap()` accepts any HTTPS XYZ raster source. It loads up to three tiles
concurrently, caches encoded tiles below persistent platform state, composites
at most 64 visible tiles into one 1920x1080 texture, and replaces the active
texture only after the new generation is complete. Normal frames therefore
draw one GPU texture and perform no tile decoding or composition on the CPU.

```js
const map = fx.tileMap({
  source: {
    url: "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
    tileSize: 256,
    attribution: "© OpenStreetMap contributors"
  },
  center: [12.635, 55.67],
  zoom: 11.45,
  cacheDays: 7,
  filter: {
    grayscale: 1,
    invert: 1,
    contrast: 0.9,
    brightness: 0.72,
    tint: 0x25364dff
  }
});
scene.add(map);
const screenPoint = map.project(12.6561, 55.6181);
```

Applications remain responsible for provider terms and for displaying the
source attribution. Tile cache durations shorter than seven days are rejected
so the default API cannot accidentally violate the OpenStreetMap minimum.

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
