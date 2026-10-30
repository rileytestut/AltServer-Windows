# External anisette provider (Windows)

Set `ALTSERVER_ANISETTE_SERVER` in AltServer's environment before launching it to
use an external header provider instead of iTunes anisette. With the variable
unset or empty, the existing local implementation is used.

For example, in PowerShell, substitute the URL of a provider you administer or
trust, then launch your **custom-built** AltServer from that same shell:

```powershell
$env:ALTSERVER_ANISETTE_SERVER = 'https://anisette.example.test/'
& '.\AltServer.exe'
```

This source change does not update an installed AltServer executable. A normal
Windows installation still needs the existing device communication dependencies.
Remove the environment variable and restart AltServer to return to local anisette.

## Provider contract

The URL is a base URL with two credential-free GET endpoints:

- `<base>/v3/client_info` returns `client_info` and `user_agent` strings.
- `<base>/` returns the header dictionary below, using string values.

```json
{
  "X-Apple-I-MD-M": "<machine ID>",
  "X-Apple-I-MD": "<fresh one-time password>",
  "X-Apple-I-MD-LU": "<local user ID>",
  "X-Apple-I-MD-RINFO": "17106176",
  "X-Mme-Device-Id": "<device ID>",
  "X-Apple-I-SRL-NO": "0",
  "X-MMe-Client-Info": "<exact client_info from metadata>",
  "X-Apple-I-Client-Time": "<current ISO 8601 timestamp>",
  "X-Apple-Locale": "en_US",
  "X-Apple-I-TimeZone": "UTC"
}
```

This is the header-provider API exposed by some anisette servers. It is **not**
a new implementation of the per-client v3 provisioning/WebSocket protocol. A
server exposing only `/v3/get_headers` and `/v3/provisioning_session` is not
compatible with this adapter. The provider manages its own provisioning state.

There is no default public provider. HTTPS and normal certificate validation are
required, except that HTTP is accepted for literal loopback/localhost providers.
Redirects are disabled; each request has a 15-second SDK timeout and a 16 KiB
response limit. Provider responses and authentication payloads are not added to
diagnostics. Apple credentials and verification codes go only to Apple's existing
authentication endpoints, never to this provider.

## Sign-in behavior

GrandSlam and verification requests use the provider's user agent together with
its client info. Before each sign-in/verification request, fresh data is fetched;
GrandSlam's `cpd` dictionary is updated too, including the app-token request after
verification. Provider-backed requests use fresh HTTP clients with certificate
validation enabled.

The machine ID, local user ID, device ID, serial number, client info and user agent
are pinned for the sign-in. If the provider changes identity mid-session, sign-in
stops instead of combining one session's tokens with another identity. Start a new
sign-in after resolving the provider change. Public shared providers may impose
their own limits; a provider serving unstable identities is unsuitable here.

The trusted-device flow also handles `hsc=409` with an absent/empty `au`: it prompts
for Apple's already-pushed code without requesting another code. Non-2xx HTTP
responses are reported by status instead of passing HTML to the plist parser.
No automatic credential or code-delivery retries are added.

The existing AltStore TCP response format, device selection and USB installation
path are retained. Local ADI state is not reset for external-provider failures.

## Validation boundary

The isolated tests exercise native C++ parsing, refresh identity checks, actual
loopback HTTP requests, malformed/oversized responses, redirects and timeouts.
They do not sign into an Apple account or install apps. A full AltServer build and
an end-to-end Apple sign-in/2FA/install test are still required before release.
