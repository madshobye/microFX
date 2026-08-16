# microFX Peer Bridge

Optional remote project transport for microFX. It is deliberately outside
`engine/`: rendering does not depend on WebRTC, PeerJS, Wi-Fi, or a specific
operating system.

The device accepts a PeerJS data connection and exposes a small JSON protocol:

| Request | Purpose |
| --- | --- |
| `system.ping` | Verify the application protocol and report persistence, active-project, and renderer state |
| `project.list` | List project folders and identify the active project |
| `project.create` | Create a project with `main.js`, `assets/`, and metadata |
| `project.get` | Return `main.js`, folders, and a deterministic asset manifest |
| `code.put` | Atomically replace `main.js` |
| `project.save-run` | Atomically save `main.js`, select the project, and request one health-checked activation |
| `asset.get` | Return one asset as base64 with its path and byte size |
| `asset.put` | Atomically write a base64-encoded asset |
| `asset.folder.create` | Create a validated project-relative asset folder |
| `asset.get.chunk` | Read a bounded binary-safe segment of an asset |
| `asset.upload.status` | Initialize or resume a persistent partial upload |
| `asset.upload.chunk` | Idempotently append one bounded upload segment |
| `asset.upload.commit` | Atomically install a complete upload and snapshot once |
| `asset.delete` | Delete an asset |
| `project.activate` | Idempotently signal the platform adapter using a caller-stable activation token |
| `revision.get` | Inspect snapshot code, metadata, and assets without changing the project |
| `revision.restore` | Atomically restore a whole-project snapshot |

The bridge uses [sepfy/libpeer](https://github.com/sepfy/libpeer), pinned by the
platform package, with full usrsctp for fragmented signaling and protocol messages.
libpeer is C despite the service being C++; its embedded-oriented API is a
better fit here than a Go/Pion runtime. Smaller platforms may select libpeer's
internal SCTP path and layer chunking above the protocol instead.

Project data lives in `/data/apps/projects/<name>`. `/data/apps/current` is an
atomic symlink selecting the active project. Previous code, `project.json`, and
asset trees are kept as whole-project snapshots under each project's
`revisions/` directory, with a bounded history of 20. Inspection is read-only;
restoration first preserves the current state as another snapshot. Legacy
code-only revisions remain readable and restorable. The PeerJS device ID is read
from `/data/config/peer-id`. The DG1 first-boot identity helper creates a stable
human-readable default there; a later portal edit remains authoritative.

Assets remain binary-safe on the JSON wire. Studio uses chunks of at most 48
KiB while the device enforces a 128 KiB decoded chunk ceiling and a 16 MiB
asset ceiling. Partial uploads live in a project-local staging directory that
is excluded from asset manifests and revisions. The deterministic upload token,
declared path, and total size let Studio continue after reconnecting or after a
bridge restart. Replayed acknowledged chunks are accepted only when their bytes
match. Legacy `asset.get` and `asset.put` remain supported.

Nested asset paths create their parent directories transactionally. Folder and
file operations share the same canonical project-boundary checks, including
symlink escape protection.

Every device-side request and response is logged with negotiation number,
DataChannel SID, request ID, command/response type, byte count, and success.
Source code and asset payloads are deliberately omitted. Together with
Studio's matching interaction trace, this distinguishes signaling, SCTP,
application-protocol, persistence, activation, and renderer failures.

`protocol.json` is the machine-readable command inventory. It classifies each
operation as read, write, or control, records its normal response type, and
marks the exact read-only subset used by Studio's interaction check. The host
conformance gate proves that the inventory, real handler branches, Studio
requests, diagnostics, and real-handler tests do not drift apart.

`project.save-run` persists code, selects the project, and requests a supervisor
reload. Once installed, the new code and selection remain authoritative even if
the reload request cannot be published; the command fails loudly and never
revives an older application. Its caller-stable activation token
makes a replay after a lost transport acknowledgement return the existing
state without another revision or renderer restart. `project.activate`
remains available for selection and Restore & Run; it uses the same idempotent
token contract and likewise leaves the requested selection installed if reload
publication fails.

`project.create` accepts a caller-stable operation token. A replay with the
same project/token pair returns success, while an unrelated operation still
gets a name collision. Revision restoration compares the complete snapshot
(code, metadata, and assets) with the editable project before preserving the
old state; replaying an already-applied restore is therefore a no-op rather
than another revision.

## Dependency boundary

- `services/peer-bridge`: portable Linux service and project protocol
- `platforms/imx6dl-dg1`: Buildroot package and init integration
- `web/editor`: browser-side PeerJS/Ace client
- `engine`: no peer or network dependency
