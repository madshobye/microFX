# microFX Studio

microFX Studio is the static Ace/PeerJS editor for the optional
`services/peer-bridge` service.

Serve this directory from any static web server. For example:

```sh
python3 -m http.server 8080 --directory web/editor
```

Then open `http://localhost:8080`, enter the Peer ID configured on the
provisioning portal, and connect. The editor uses PeerJS `serialization: raw`
and sends UTF-8 JSON over a reliable WebRTC DataChannel.

Studio does not call a DataChannel “online” until a versioned `system.ping`
round trip succeeds. This prevents a WebRTC-open/libpeer-stalled connection
from enabling Save & Run and then failing later as an ambiguous `project.get`
timeout. The visible interaction trace correlates request IDs, command names,
response types, results, and latency without recording code or asset payloads.
The **Check** button runs a read-only handshake, project-list, selected-project,
and console pass and names the first failing stage.

Retrieving a project loads `main.js`, the asset manifest, and the existing
folder list. The Files panel can create an empty folder, upload individual
files to a chosen relative destination, or upload a browser-selected directory
while preserving its subfolders. This includes model textures and `.vs`/`.fs`
shader assets; absolute paths and `..` traversal are rejected. Asset transfers
use bounded base64 chunks rather than one large data-channel message. Uploads
query their acknowledged offset before sending, so selecting the same file
after a disconnect resumes the partial transfer. Studio also keeps an active
upload waiting through its automatic reconnect and continues without asking
the user to select the file again. Downloads restart safely after reconnect;
an intentional Disconnect cancels waiting transfers. Completion atomically
commits the file and creates one revision. The original whole-file commands
remain available for compatibility with older Studio clients.

Each selected file is an independent upload. Uploading one file does not
resend, replace, or delete any other project asset, and a failed file does not
prevent the remaining selected files from being attempted.

All interactive project requests use the same reconnect session and wait for a
replacement DataChannel when the radio drops. Save & Run is one device-side
transaction that persists `main.js`, selects the project, and publishes one
health-checked activation. A publication failure is reported while the new
code and selection remain installed; Studio never revives older code
implicitly. Save & Run and Restore & Run carry
stable activation tokens across retries, so an acknowledgement lost after the
device accepted the request does not restart the renderer a second time.
Project creation carries its own operation token, and replaying an
already-applied restore does not create a duplicate revision.

The revision panel represents whole-project snapshots: code, project details,
and assets. **Inspect** is read-only. **Restore** reverts the editable project,
while **Restore & Run** also waits for the renderer health check before reporting
success. The pre-restore state is retained as a new snapshot.

PeerJS 1.5.5 and the minimal Ace 1.39.1 JavaScript-mode runtime are vendored in
`vendor/`, including their upstream license files. Studio therefore loads with
no CDN access. Peer discovery and WebRTC negotiation still require whatever
PeerJS signalling service is selected by `app.js`; vendoring the browser code
does not turn that external service into a local one. `npm test` verifies that
the HTML has no remote script or stylesheet dependencies, every required Ace
module is present, and the pinned files retain their recorded SHA-256 values.

The dependency-free Node suite includes a stateful browser/PeerJS harness that
imports the real `app.js` and drives the actual controls. It covers connection,
retrieval, project creation and switching, plain Save, Save & Run, project
details, one-file upload, asset download and deletion, revision inspection,
Restore, Restore & Run, console clearing, and disconnection. It verifies that
an unrelated asset remains present, activation identity and renderer-health
acknowledgement, in addition to the lower-level protocol and real
bridge-restart tests. A separate integration test connects the exact Studio
action controller to the real C++ handler and real platform supervisor. It
proves Save & Run serialization, persistence, activation health, visible
renderer failure, and a successful subsequent recovery without a browser mock
server at that boundary.
