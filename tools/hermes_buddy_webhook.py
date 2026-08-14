#!/usr/bin/env python3
import json
import os
import re
import sys
import threading
import time
from collections import OrderedDict, defaultdict, deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from hashlib import sha256
from secrets import compare_digest
from urllib.parse import urlparse

HOST = os.environ.get('HERMES_BUDDY_HOST', '127.0.0.1')
PORT = int(os.environ.get('HERMES_BUDDY_PORT', '8788'))
TOKEN = os.environ.get('HERMES_BUDDY_TOKEN', '')
MAX_BODY_BYTES = int(os.environ.get('HERMES_BUDDY_MAX_BODY_BYTES', '4096'))
MAX_WORKERS = int(os.environ.get('HERMES_BUDDY_MAX_WORKERS', '2'))
RATE_LIMIT_WINDOW = int(os.environ.get('HERMES_BUDDY_RATE_WINDOW', '60'))
RATE_LIMIT_PER_DEVICE = int(os.environ.get('HERMES_BUDDY_RATE_PER_DEVICE', '30'))
REQUEST_TTL_SECONDS = int(os.environ.get('HERMES_BUDDY_REQUEST_TTL_SECONDS', '300'))
MAX_SEEN_REQUESTS = int(os.environ.get('HERMES_BUDDY_MAX_SEEN_REQUESTS', '1024'))
MAX_RATE_LIMIT_DEVICES = int(os.environ.get('HERMES_BUDDY_MAX_RATE_LIMIT_DEVICES', '1024'))
MIN_TOKEN_LENGTH = 32

ACTION_COPY = {
    'secretary.memo.capture': ('MEMO', 'めも受け取ったよ', 'あとで忘れないようにするね'),
    'secretary.reminder.quick_add': ('REMIND', 'りまいんど受け取ったよ', 'あとでちゃんと声かけするね'),
    'secretary.task.next': ('NEXT', 'いっしょに見るね', '次にやることを確認しよう'),
    'secretary.mode.home': ('HOME', 'おかえりなさい', 'エルメスちゃん待機中だよ'),
}
SAFE_ID_RE = re.compile(r'^[A-Za-z0-9._:-]{1,80}$')

_seen_requests = OrderedDict()
_device_hits = defaultdict(deque)
_state_lock = threading.Lock()


def validate_startup_config(token: str = TOKEN, host: str = HOST):
    if not token:
        raise RuntimeError('HERMES_BUDDY_TOKEN is required')
    if token == 'change-me' or len(token) < MIN_TOKEN_LENGTH:
        raise RuntimeError('HERMES_BUDDY_TOKEN must be a non-default high-entropy value of at least 32 characters')
    if host in {'0.0.0.0', '::'} and os.environ.get('HERMES_BUDDY_ALLOW_PUBLIC_BIND') != '1':
        raise RuntimeError('public bind requires HERMES_BUDDY_ALLOW_PUBLIC_BIND=1 and a reverse proxy')
    if host in {'0.0.0.0', '::'}:
        public_base_url = os.environ.get('HERMES_BUDDY_PUBLIC_BASE_URL', '')
        if not public_base_url.startswith('https://'):
            raise RuntimeError('public bind requires HERMES_BUDDY_PUBLIC_BASE_URL=https://... behind TLS-terminating proxy')


def fallback_payload(action: str):
    title, message, body = ACTION_COPY[action]
    return {
        'ok': True,
        'message': message,
        'display': {'title': title, 'body': body},
    }


def _error_payload(title: str, body: str):
    return {'ok': False, 'message': body, 'display': {'title': title, 'body': body[:40]}}


def validate_payload(payload: dict, now=None):
    now = int(now if now is not None else time.time())
    if not isinstance(payload, dict):
        raise ValueError('payload must be object')
    request = payload.get('request')
    device = payload.get('device')
    context = payload.get('context', {})
    if not isinstance(request, dict) or not isinstance(device, dict) or not isinstance(context, dict):
        raise ValueError('request/device/context must be objects')
    action = request.get('action')
    request_id = request.get('id')
    timestamp = request.get('timestamp', now)
    device_id = device.get('id')
    if action not in ACTION_COPY:
        raise ValueError('unknown action')
    if not isinstance(request_id, str) or not SAFE_ID_RE.fullmatch(request_id):
        raise ValueError('invalid request id')
    if not isinstance(device_id, str) or not SAFE_ID_RE.fullmatch(device_id):
        raise ValueError('invalid device id')
    if not isinstance(timestamp, int) or abs(now - timestamp) > REQUEST_TTL_SECONDS:
        raise ValueError('invalid request timestamp')
    for key in ('battery', 'wifi_rssi'):
        if key in context and not isinstance(context[key], (int, float, str)):
            raise ValueError(f'invalid context field: {key}')
        if isinstance(context.get(key), str) and len(context[key]) > 32:
            raise ValueError(f'context field too long: {key}')
    return action, device_id, request_id


def check_replay(device_id: str, request_id: str, now=None):
    now = int(now if now is not None else time.time())
    key = f'{device_id}:{request_id}'
    with _state_lock:
        expired = [k for k, ts in _seen_requests.items() if now - ts > REQUEST_TTL_SECONDS]
        for k in expired:
            _seen_requests.pop(k, None)
        if key in _seen_requests:
            raise ValueError('duplicate request id')
        _seen_requests[key] = now
        while len(_seen_requests) > MAX_SEEN_REQUESTS:
            _seen_requests.popitem(last=False)


def check_rate_limit(device_id: str, now=None):
    now = int(now if now is not None else time.time())
    with _state_lock:
        for key in list(_device_hits.keys()):
            hits_for_key = _device_hits[key]
            while hits_for_key and now - hits_for_key[0] >= RATE_LIMIT_WINDOW:
                hits_for_key.popleft()
            if not hits_for_key:
                _device_hits.pop(key, None)
        while len(_device_hits) >= MAX_RATE_LIMIT_DEVICES and device_id not in _device_hits:
            _device_hits.pop(next(iter(_device_hits)))
        hits = _device_hits[device_id]
        if len(hits) >= RATE_LIMIT_PER_DEVICE:
            raise ValueError('rate limit exceeded')
        hits.append(now)


def parse_content_length(headers):
    raw = headers.get('Content-Length')
    if raw is None:
        raise ValueError('Content-Length required')
    try:
        length = int(raw)
    except ValueError as exc:
        raise ValueError('invalid Content-Length') from exc
    if length < 0:
        raise ValueError('invalid Content-Length')
    if length > MAX_BODY_BYTES:
        raise OverflowError('request body too large')
    return length


def is_authorized(headers, token: str = TOKEN):
    auth = headers.get('Authorization', '')
    prefix = 'Bearer '
    if not auth.startswith(prefix):
        return False
    provided_digest = sha256(auth[len(prefix):].encode('utf-8')).digest()
    expected_digest = sha256(token.encode('utf-8')).digest()
    return compare_digest(provided_digest, expected_digest)


class BoundedThreadingHTTPServer(ThreadingHTTPServer):
    daemon_threads = True

    def process_request(self, request, client_address):
        if not hasattr(self, '_active_requests'):
            import threading
            self._active_requests = threading.BoundedSemaphore(MAX_WORKERS)
        if not self._active_requests.acquire(blocking=False):
            close_request = getattr(request, 'close', None)
            if callable(close_request):
                close_request()
            return
        try:
            super().process_request(request, client_address)
        except Exception:
            self._active_requests.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self._active_requests.release()


class Handler(BaseHTTPRequestHandler):
    server_version = 'HermesBuddyWebhook/0.2'

    def _send_json(self, status: int, obj: dict):
        data = json.dumps(obj, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == '/health':
            self._send_json(200, {'ok': True, 'service': 'hermes-buddy-webhook'})
            return
        self._send_json(404, {'ok': False, 'error': 'not found'})

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path != '/buddy/actions':
            self._send_json(404, _error_payload('ERROR', 'not found'))
            return
        if not is_authorized(self.headers):
            self._send_json(401, _error_payload('AUTH', 'unauthorized'))
            return
        try:
            length = parse_content_length(self.headers)
        except OverflowError:
            self._send_json(413, _error_payload('ERROR', 'body too large'))
            return
        except ValueError as exc:
            self._send_json(400, _error_payload('ERROR', str(exc)))
            return
        raw = self.rfile.read(length)
        try:
            payload = json.loads(raw.decode('utf-8')) if raw else {}
            action, device_id, request_id = validate_payload(payload)
            check_rate_limit(device_id)
            check_replay(device_id, request_id)
        except Exception as exc:
            self._send_json(400, _error_payload('ERROR', str(exc)))
            return
        response = fallback_payload(action)
        sys.stderr.write(json.dumps({'event': 'accepted', 'action': action, 'device_id': device_id, 'request_id': request_id}, ensure_ascii=False) + '\n')
        sys.stderr.flush()
        self._send_json(200, response)

    def log_message(self, format, *args):
        # Keep credentials out of access logs: log method only, not full URL or Authorization.
        sys.stderr.write('%s - - [%s] %s\n' % (self.address_string(), self.log_date_time_string(), self.command))
        sys.stderr.flush()


if __name__ == '__main__':
    validate_startup_config()
    httpd = BoundedThreadingHTTPServer((HOST, PORT), Handler)
    print(f'Hermes Buddy webhook listening on http://{HOST}:{PORT}/buddy/actions', flush=True)
    httpd.serve_forever()
