"""Exercise the production HTTP adapter using loopback-only synthetic fixtures."""
import datetime
import http.server
import json
import subprocess
import sys
import threading


class Provider(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    requests_seen = []
    root_requests = {}
    violations = []

    def log_message(self, *_):
        pass

    def do_GET(self):
        self.requests_seen.append(self.path)
        for name in ("Authorization", "Cookie", "security-code"):
            if name in self.headers:
                self.violations.append(name)
        scenario = self.path.split("/")[1]
        if scenario == "timeout":
            threading.Event().wait(20)
            self.close_connection = True
            return
        metadata = self.path.endswith("v3/client_info")
        status = 200
        headers = {}
        payload = {"client_info": "fixture-client", "user_agent": "fixture-agent"}
        if not metadata:
            count = self.root_requests.get(scenario, 0) + 1
            self.root_requests[scenario] = count
            payload = {
                "X-Apple-I-MD-M": "fixture-machine", "X-Apple-I-MD": f"fixture-otp-{count}",
                "X-Apple-I-MD-LU": "fixture-local-user", "X-Apple-I-MD-RINFO": "17106176",
                "X-Mme-Device-Id": "fixture-device", "X-Apple-I-SRL-NO": "0",
                "X-MMe-Client-Info": "fixture-client", "X-Apple-Locale": "en_US",
                "X-Apple-I-TimeZone": "UTC",
                "X-Apple-I-Client-Time": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            }
            if scenario == "rotate" and count > 1:
                payload["X-Apple-I-MD-M"] = "different-machine"
            if scenario == "mismatch":
                payload["X-MMe-Client-Info"] = "different-client"
        body = json.dumps(payload).encode()
        if scenario == "bad-json":
            body = b"<html>PRIVATE_FIXTURE_BODY</html>"
        elif scenario == "http-error":
            status, body = 503, b"PRIVATE_FIXTURE_BODY"
        elif scenario == "redirect":
            status = 302
            headers["Location"] = "/redirect-target/v3/client_info"
        elif scenario in ("oversize", "chunked-oversize"):
            body = b"x" * 20000
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Connection", "close")
        self.close_connection = True
        for name, value in headers.items():
            self.send_header(name, value)
        if scenario == "chunked-oversize":
            self.send_header("Transfer-Encoding", "chunked")
        else:
            self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            if scenario == "chunked-oversize":
                for offset in range(0, len(body), 1024):
                    chunk = body[offset:offset + 1024]
                    self.wfile.write(f"{len(chunk):x}\r\n".encode() + chunk + b"\r\n")
                self.wfile.write(b"0\r\n\r\n")
            else:
                self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


def main():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Provider)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    cases = ["ok", "rotate", "mismatch", "bad-json", "http-error", "redirect", "oversize", "chunked-oversize", "timeout"]
    try:
        for scenario in cases:
            mode = "--fetch-http" if scenario == "ok" else "--reject-http"
            url = f"http://127.0.0.1:{server.server_port}/{scenario}/"
            result = subprocess.run([sys.argv[1], mode, url], capture_output=True, text=True, timeout=40)
            assert result.returncode == 0, f"{scenario}: {result.stdout}\n{result.stderr}"
            assert "PRIVATE_FIXTURE_BODY" not in result.stdout + result.stderr
            assert any(path.startswith(f"/{scenario}/") for path in Provider.requests_seen)
            print(f"PASS HTTP {scenario}")
        assert not any("redirect-target" in path for path in Provider.requests_seen)
        assert Provider.root_requests["ok"] == 2, "refresh must fetch new data"
        assert Provider.root_requests["rotate"] == 2, "rotation must be detected on refresh"
        assert not Provider.violations, "Provider received sensitive request headers"
        print(f"{len(cases)} HTTP integration tests passed")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
