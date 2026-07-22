# guacamole-server patches

This directory contains redistributable patch artifacts for this fork until the
changes are upstreamed into Apache Guacamole.

## Patch index

| Patch | Status | Tracking issue |
| --- | --- | --- |
| [`0001-add-text-output-mode.patch`](0001-add-text-output-mode.patch) | Implemented on `feature/3-text-output-mode` | [ciroiriarte/guacamole-server#3](https://github.com/ciroiriarte/guacamole-server/issues/3) |

## `text-output` raw terminal output mode

The patch adds an opt-in `text-output` connection parameter for terminal-backed
protocols (SSH, telnet, Kubernetes):

- `text-output=true`: tee raw PTY bytes to an outbound `STDOUT` pipe while keeping
  graphical terminal rendering enabled.
- `text-output=raw`: headless/raw mode for dedicated native clients; raw PTY bytes
  are emitted through `STDOUT` and graphical terminal rendering is skipped.

The outbound pipe is named `STDOUT`, uses mimetype `application/octet-stream`,
carries base64 `blob` payloads, and is closed with `end`. It is allocated on the
connection owner's user socket so client `ack` instructions route back to the
stream handler. Clients must `ack` every received `blob`; guacd bounds text-output
backlog at 16 unacknowledged blobs and drops further buffered output instead of
blocking the PTY/read loop.

`disable-copy` is honored, and raw pipe contents are not logged or recorded by
default.

## Applying

From a clean Apache Guacamole server checkout at the pinned base used by this
fork, run:

```sh
git am /path/to/0001-add-text-output-mode.patch
```

The patch artifact was generated from this branch with:

```sh
git format-patch --stdout $(git merge-base HEAD origin/main)..HEAD > patches/0001-add-text-output-mode.patch
```

## Building / smoke validation

After applying, build guacd using the normal project bootstrap/configure flow for
your platform. For this fork, the following local checks cover the text-output
surface:

```sh
util/manual-tests/text-output-tunnel-smoke.py --help
python3 -m py_compile util/manual-tests/text-output-tunnel-smoke.py \
    util/manual-tests/text-output-guacd-e2e.py \
    util/manual-tests/text-output-k8s-exec-mock.py
make check
```

Manual end-to-end validation is documented in
[`util/manual-tests/README-text-output-e2e.md`](../util/manual-tests/README-text-output-e2e.md).

Upstream tracking: https://github.com/ciroiriarte/guacamole-server/issues/3
