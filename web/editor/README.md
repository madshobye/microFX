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

Retrieving a project loads `main.js` and the asset manifest. Each listed asset
has a Download action that requests its binary content with `asset.get`; Upload
uses the matching base64 `asset.put` command.

The CDN script versions are pinned. They can later be vendored into this
directory for a completely offline editor without changing the protocol.
