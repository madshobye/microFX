# microFX Peer Bridge

Optional remote project transport for microFX. It is deliberately outside
`engine/`: rendering does not depend on WebRTC, PeerJS, Wi-Fi, or a specific
operating system.

The device accepts a PeerJS data connection and exposes a small JSON protocol:

| Request | Purpose |
| --- | --- |
| `project.get` | Return `main.js` and a deterministic asset manifest |
| `code.put` | Atomically replace `main.js` |
| `asset.get` | Return one asset as base64 with its path and byte size |
| `asset.put` | Atomically write a base64-encoded asset |
| `asset.delete` | Delete an asset |
| `project.activate` | Signal the platform adapter to reload the project |

The bridge uses [sepfy/libpeer](https://github.com/sepfy/libpeer), pinned by the
platform package, with full usrsctp for fragmented JS and asset messages.
libpeer is C despite the service being C++; its embedded-oriented API is a
better fit here than a Go/Pion runtime. Smaller platforms may select libpeer's
internal SCTP path and layer chunking above the protocol instead.

Project data lives in `/data/apps/current`. The PeerJS device ID is read
from `/data/config/peer-id` and defaults to `microfx-demo`.

Assets remain binary-safe on the JSON wire: `asset.get` returns
`{type:"asset", path, size, encoding:"base64", content}`, and `asset.put`
accepts the same base64 content representation. The DataChannel provides
reliable fragmentation for messages larger than one SCTP packet.

## Dependency boundary

- `services/peer-bridge`: portable Linux service and project protocol
- `platforms/imx6dl-dg1`: Buildroot package and init integration
- `web/editor`: browser-side PeerJS/Ace client
- `engine`: no peer or network dependency
