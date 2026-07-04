Terminal text-output mode
=========================

This branch adds an opt-in terminal connection parameter named
`text-output` for SSH, telnet, and Kubernetes connections.

When `text-output` is set to `true`, guacd tees the raw bytes received from the
remote terminal/PTY to an outbound Guacamole pipe stream named `STDOUT` while
continuing to render the normal graphical terminal display. Clients can consume
that pipe to implement CLI-style access to terminal sessions without scraping
pixels from the display.

Supported protocols
-------------------

* SSH: `text-output=true`
* Telnet: `text-output=true`
* Kubernetes: `text-output=true`

The parameter is intentionally opt-in. Existing connections continue to behave
normally unless the parameter is explicitly enabled.

Security and clipboard/copy behavior
------------------------------------

The `STDOUT` pipe exposes the raw terminal byte stream to the Guacamole client.
This is effectively a copy/export channel. For that reason, the implementation
honors the existing copy restriction used by terminal protocols:

* If copy/clipboard output is disabled for the connection, `text-output` is not
  opened.
* If copy/clipboard output is allowed and `text-output=true`, guacd opens the
  `STDOUT` pipe and writes raw terminal bytes to it.

Manual tunnel smoke test
------------------------

A reusable manual smoke test is provided at:

    util/manual-tests/text-output-tunnel-smoke.py

It validates the full client-facing path:

1. authenticate to the Guacamole REST API;
2. open a connection through `/websocket-tunnel`;
3. verify that guacd opens the outbound `STDOUT` pipe;
4. type a harmless `printf` command through Guacamole keyboard instructions;
5. verify that the command output returns through the `STDOUT` pipe.

The script requires the Python `websocket-client` package:

    python3 -m pip install websocket-client

Example against the local test deployment used during development:

    util/manual-tests/text-output-tunnel-smoke.py \
        --url http://10.2.0.186:8080/guacamole \
        --username guacadmin \
        --password guacadmin \
        --data-source postgresql \
        --connection-id 1

The same values can also be provided with environment variables:

    GUAC_URL=http://10.2.0.186:8080/guacamole \
    GUAC_USERNAME=guacadmin \
    GUAC_PASSWORD=guacadmin \
    GUAC_CONNECTION_ID=1 \
        util/manual-tests/text-output-tunnel-smoke.py

The command exits with status 0 and prints:

    RESULT: tunnel STDOUT pipe smoke test passed

when the `STDOUT` pipe is present and the marker emitted by the remote shell is
received through that pipe.

For protocol-specific smoke checks where the backend target is intentionally
minimal or unreachable, `--pipe-only` can be used to validate that guacd opens
the protocol's `STDOUT` pipe without requiring an interactive shell command to
complete:

    util/manual-tests/text-output-tunnel-smoke.py \
        --url http://10.2.0.186:8080/guacamole \
        --username guacadmin \
        --password guacadmin \
        --data-source postgresql \
        --connection-id 2 \
        --pipe-only

For shell prompts that are not the default `$ `, use `--prompt` to select the
bytes the smoke test should wait for before typing the marker command. For
example, BusyBox `/bin/sh` inside Kubernetes commonly prompts with `# `:

    util/manual-tests/text-output-tunnel-smoke.py \
        --url http://10.2.0.186:8080/guacamole \
        --username guacadmin \
        --password guacadmin \
        --data-source postgresql \
        --connection-id 3 \
        --prompt '# '

Development validation snapshot
-------------------------------

The SSH implementation was validated end-to-end on July 4, 2026 against a test
Guacamole stack:

* Guacamole web application 1.6.0 on Tomcat 9
* patched guacd from this branch
* PostgreSQL authentication/connection store
* connection `1`: `SSH text-output (localhost)` with `text-output=true`

Validation performed:

* REST login returned a valid token.
* `GET /api/session/data/postgresql/connections` listed the SSH test
  connection.
* `/websocket-tunnel` opened successfully for the connection.
* The tunnel advertised `PIPE stream=1 mimetype=application/octet-stream
  name=STDOUT`.
* A harmless `printf` marker command was sent through the Guacamole keyboard
  protocol and the marker was received through the `STDOUT` pipe.

Additional protocol validation was performed against real Telnet and
Kubernetes targets on a clean openSUSE Leap 16.0 VM (`10.2.0.190`) provisioned
from the CI template on VLAN 100:

* Telnet: Guacamole connection `2` targeted a `socat` TCP listener backed by a
  real PTY shell on port `2323`; the marker
  `GUAC_TELNET_OUTPUT_SMOKE_OK_20260704` round-tripped through `STDOUT`.
* Kubernetes: Guacamole connection `3` targeted a single-node k3s cluster and
  executed `/bin/sh` in pod `default/guac-smoke`; the marker
  `GUAC_K8S_OUTPUT_SMOKE_OK_20260704` round-tripped through `STDOUT`.
* Clean VM build/check: the branch built successfully on openSUSE Leap 16.0
  with SSH, Kubernetes, guacd, and CUnit tests enabled. Telnet was not built on
  that VM because Leap 16.0 did not provide `libtelnet-devel`; Telnet runtime
  validation used the deployed patched guacd.
