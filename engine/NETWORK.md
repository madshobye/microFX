# JavaScript networking

microFX networking is owned by the active JavaScript application. HTTP and
socket progress is polled without blocking from the renderer loop; there is no
shell command, helper process, daemon, or worker thread.

## HTTP client

`fetch()` and `fx.net.fetch()` currently implement bounded HTTP(S) GET. They
follow at most five redirects, time out after 20 seconds, verify HTTPS against
the system CA bundle, allow eight concurrent requests, and reject bodies larger
than 2 MiB.

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
`arrayBuffer()`. The binary response is still subject to the same 2 MiB
limit.

## Scientific binary data

`fx.data.decode()` decodes a named numeric dataset directly from an HDF5
`ArrayBuffer`. It accepts integer and floating-point grids with one to four
dimensions. Optional `start`, `count`, and `stride` arrays select a bounded
hyperslab before it enters the JavaScript heap; `attributes` requests metadata
from named HDF5 objects. Unsupported types, invalid selections, inputs above
2 MiB, and decoded selections above 12 MiB fail loudly.

```js
const bytes = await fetch(radarUrl).then(response => response.arrayBuffer());
const radar = fx.data.decode(bytes, {
  format: "hdf5",
  dataset: "/dataset1/data1/data",
  stride: [4, 4],
  attributes: ["/what", "/where", "/dataset1/data1"]
});

// radar.data is the matching typed array. Shape describes the selected grid;
// sourceShape describes the full dataset.
const firstValue = radar.data[0];
```

The decoder performs no rendering and contains no source-specific radar logic.
Projection, thresholding, contours, and retained geometry remain ordinary
JavaScript application code.

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
Sources with finite native coverage can set integer `minZoom` and `maxZoom`;
the viewport remains geographically aligned while its nearest native zoom is
scaled for display.

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
const location = map.unproject(screenPoint.x, screenPoint.y);
```

The cached map can become a general GPU texture without another map decode or
CPU image pass. The same API accepts image assets. A shader may combine either
source with one small RGBA field and a bounded parameter vector:

```js
const weather = fx.texture(map)
  .shader("assets/shaders/weather.fs")
  .field(30, 20, weatherRgbaBytes)
  .params([sunElevation, sunDirectionX, sunDirectionY])
  .stage("overlay")
  .blend(true);
```

The shader receives `uTexture`, optional `uField`, `uFieldSize`, `uResolution`,
`uTime`, and `uParams[8]`. Fields are limited to 64×64 RGBA cells and parameters
to 32 floats. These limits keep the feature a predictable GPU composition path,
not an unbounded CPU pixel API.

The map remains a separate background layer. Retained trains, airports, and
other symbols render above it, followed by transparent GPU texture overlays.
Text and diagnostics remain the interface layer above those passes.

`fx.maps.earth()` creates an aligned worldwide image pair. Its `day` map uses
Esri World Imagery and its `night` map uses NASA Black Marble / VIIRS's
lights-only composite pinned to 2016-01-01. The night source is native through
zoom 8 and intentionally over-zooms for city views. Only the visible viewport
tiles are requested; the appliance never downloads a global raster.

```js
const earth = fx.maps.earth({
  center: [12.5683, 55.6761],
  zoom: 11.25
});
scene.add(earth.day);
scene.add(earth.night);
earth.hide(); // retained as texture sources without drawing either map directly

const view = fx.texture(earth.day)
  .secondary(earth.night)
  .shader("assets/shaders/day-night.fs")
  .params([nightOpacity])
  .stage("background")
  .blend(false);
```

The shader receives the two aligned images as `uTexture` and `uTexture2`.
The pair's `center()` and `zoom()` methods update both sources atomically. A
different fixed `nightDate` may be supplied as `YYYY-MM-DD`; invalid or missing
NASA dates fail through the normal tile request path instead of silently
selecting another image.

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
