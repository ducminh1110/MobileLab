# Concept: Connect a Mac (planned, not implemented)

Status: **concept only.** Nothing in this document exists in the code yet. It records what was asked for so
the next session can build it.

## Why

iOS Simulators only run on macOS. The MobileLab backend and its interfaces (web dashboard, CLI, MCP) can
run anywhere: a Linux box, a laptop, a CI machine. To drive *real* simulators from those, the user connects a
Mac. Today the only option is to install and start the backend on the Mac by hand.

## The user story

In the web dashboard, the user clicks **Connect Mac...**, types `username@address`, gives a password or
uploads an SSH private key, and MobileLab logs into the Mac over SSH, installs its remote server on the Mac
by itself, starts it, and sets up remote work: from then on the Mac's simulators appear in the Devices
navigator and test runs can be sent to them.

## Interface (Xcode layout, see `xcode-interface.md`)

* Entry points: a **Mac hosts** group at the top of the Devices navigator with a `+` footer button
  (*Connect Mac...*), an item in the destination popup (*Connect Mac...*), and a row on the Welcome editor.
* **Connect sheet** (a modal like Xcode's sheets):
  * *Address*: `user@host` or `user@host:port` (validated, port defaults to 22).
  * *Authentication*: segmented `Password | Private key`. Private key = choose a file (or paste), with an
    optional passphrase field.
  * *Host key*: after the first contact the sheet shows the host key fingerprint (SHA256) and asks the user to
    trust it (or compares it with a pinned one). Connecting never proceeds silently on an unknown key.
  * Button *Connect*, disabled until the form is valid; errors appear under the field that caused them.
* **Progress** replaces the form: a list of steps with a glyph each (pending, spinner, check, cross) and the
  live command output in the debug-area console. Every failed step shows a plain-language cause and a remedy,
  and a *Retry* button. Steps: 1 Connect and verify host key; 2 Check the Mac (macOS version, architecture,
  Xcode and command line tools, free disk); 3 Prepare a Node runtime; 4 Upload the MobileLab runtime;
  5 Install the launch agent; 6 Start it and wait for `/health`; 7 Open the tunnel and register the node.
* After success the Mac appears as a node: `Mac hosts > name` with a status dot (online, offline,
  installing, error), its simulators grouped by runtime underneath, and an inspector page (address,
  fingerprint, macOS and Xcode versions, runtime version, capacity, last seen) with *Reconnect*, *Reinstall*,
  *Update* and *Disconnect and remove*.
* Toolbar capsule: the destination popup groups devices by host (`This machine`, `<Mac name>`). Runs on a
  remote Mac are labelled with the host in reports and the Reports navigator.

## How it works

```
 dashboard --REST--> controller backend --SSH (ssh2)--> Mac
                            |                             |
                            |   1. exec: sw_vers, xcodebuild -version, node -v, df
                            |   2. sftp: upload backend-runtime (scripts/package_runtime.sh output)
                            |   3. write ~/Library/LaunchAgents/dev.mobilelab.node.plist, launchctl load
                            |   4. remote backend listens on 127.0.0.1:<port> with a generated API token
                            +--- SSH local port forward --------> remote backend (loopback only)
```

* **No sudo.** Everything lives in the user's home: `~/Library/Application Support/MobileLab/` for the
  runtime and data, a per-user LaunchAgent for start at login. Nothing is installed system-wide.
* **Node.** The runtime needs Node 20+. If the Mac has none, the bootstrapper uploads a pinned official Node
  tarball for the Mac's architecture (arm64 or x64) into the runtime folder instead of asking for `sudo` or
  Homebrew.
* **The remote server is the same backend** (`backend-runtime`), started with its API bound to loopback and
  a random token. It is only reachable through the SSH tunnel; the port is never exposed on the Mac's network.
* **Federation.** The controller keeps a `RemoteNode` per Mac and talks to its backend through the tunnel.
  A node's simulators show up in the pool with `nodeId`; the dispatcher places a job on a device of a node the
  same way it places one locally, and proxies job events, output and screenshots back. Capacity is per node.
* **Resilience.** Tunnel drops reconnect with backoff; a node that stays unreachable is marked offline and
  its queued jobs wait (or fail after a timeout the user can set); jobs that were running on a lost node are
  marked failed with the reason. On reconnect the node's state is reconciled like `POST /devices/sync`.

## Security requirements

* **Credentials are never persisted.** A password or key passphrase lives in memory for the duration of the
  bootstrap and is dropped afterwards. Uploaded key files are read into memory (size limit a few KB), never
  written to disk or logged, and zeroed after use. Nothing secret goes into events, logs, error messages
  or the state file.
* **Replace the password with a key.** After the first successful login the controller generates an ed25519
  key pair for this node, appends its public key to the Mac's `~/.ssh/authorized_keys`, stores the private key
  in the OS keychain or a `0600` file under the data directory, and uses it from then on. The user can opt out
  and reconnect manually each time.
* **Host key pinning** (trust on first use with explicit confirmation); a changed key blocks the connection
  with a clear warning.
* **Only over a trusted channel.** The connect form sends credentials to the controller, so it is enabled only
  when the dashboard is served over HTTPS or from loopback; otherwise it is disabled with an explanation. The
  controller requires its own API token whenever it listens beyond loopback (`IOSLAB_API_TOKEN`).
* **Limits**: attempt rate limiting per address, connection and command timeouts, strict validation of
  `user@host` (no shell metacharacters), commands built from fixed templates, never from user input.
* Everything the bootstrapper changes on the Mac is listed to the user before it happens and can be removed
  again with *Disconnect and remove* (unload the agent, delete the folder, optionally remove the key).

## API sketch

| Method and path | Purpose |
| --- | --- |
| `POST /nodes/connect` | `{ address, auth: { type: "password", password } \| { type: "key", privateKey, passphrase? }, trustFingerprint? }`. First call without `trustFingerprint` returns `409 { fingerprint }` to confirm. Returns `202 { node }`; progress arrives as events. |
| `GET /nodes`, `GET /nodes/:id` | Nodes with status, versions, capacity, last seen |
| `POST /nodes/:id/reconnect`, `/reinstall`, `/update` | Maintenance |
| `DELETE /nodes/:id` | Unload the agent, remove the install, forget the node |
| Events | `source: "node"`, actions `connect_step`, `node_online`, `node_offline`, with `nodeId` |

## Implementation notes

* Library: `ssh2` (pure JavaScript, no native build) for exec, SFTP and port forwarding.
* New backend module `src/remote/`: `RemoteHostService` (connect, exec, upload, tunnel), `NodeBootstrapper`
  (the steps above as a resumable state machine with a fixed script per step), `NodeRegistry` (persisted
  without secrets), and node-aware device/job routing in the orchestrator.
* Tests: an in-process `ssh2` server that emulates the Mac (macOS `sw_vers`, `xcodebuild -version`, SFTP,
  `launchctl`) so every step and every failure path is covered without a real Mac; a fake remote backend for
  the tunnel and federation paths. A manual test plan for a real Mac is part of the acceptance criteria.
* Reuse: `scripts/package_runtime.sh` already produces the runtime to upload; the CLI gets `ioslab mac
  connect|list|remove` for parity; the macOS app is not needed on the remote Mac.
* Not in scope for the first version: Windows or Linux remotes, jump hosts, ssh-agent forwarding, storing
  passwords.
