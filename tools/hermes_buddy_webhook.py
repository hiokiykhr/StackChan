#!/usr/bin/env python3
import json
import os
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

HOST = os.environ.get('HERMES_BUDDY_HOST', '0.0.0.0')
PORT = int(os.environ.get('HERMES_BUDDY_PORT', '8788'))
TOKEN = os.environ.get('HERMES_BUDDY_TOKEN', 'change-me')
MODEL_TIMEOUT = int(os.environ.get('HERMES_BUDDY_TIMEOUT', '45'))

ACTION_COPY = {
    'secretary.memo.capture': ('MEMO', 'めも受け取ったよ', 'あとで忘れないようにするね'),
    'secretary.reminder.quick_add': ('REMIND', 'りまいんど受け取ったよ', 'あとでちゃんと声かけするね'),
    'secretary.task.next': ('NEXT', 'いっしょに見るね', '次にやることを確認しよう'),
    'secretary.mode.home': ('HOME', 'おかえりなさい', 'エルメスちゃん待機中だよ'),
}


def fallback_payload(action: str):
    title, message, body = ACTION_COPY.get(action, ('ERROR', '未対応だよ', '未知のアクションを受けたよ'))
    return {
        'ok': action in ACTION_COPY,
        'message': message,
        'display': {'title': title, 'body': body},
    }


def payload_summary(payload: dict):
    request = payload.get('request', {}) or {}
    device = payload.get('device', {}) or {}
    context = payload.get('context', {}) or {}
    return {
        'action': request.get('action', ''),
        'request_id': request.get('id', ''),
        'device_id': device.get('id', ''),
        'firmware': device.get('firmware', ''),
        'battery': context.get('battery', ''),
        'wifi_rssi': context.get('wifi_rssi', ''),
    }


def ask_hermes(payload: dict):
    action = payload.get('request', {}).get('action', '')
    request_id = payload.get('request', {}).get('id', '')
    device_id = payload.get('device', {}).get('id', '')
    battery = payload.get('context', {}).get('battery', '')
    wifi_rssi = payload.get('context', {}).get('wifi_rssi', '')
    fallback = json.dumps(fallback_payload(action), ensure_ascii=False)
    prompt = f'''You are Hermes-chan, a cute but practical hardware buddy for the user.
A small M5StickS3 device sent a webhook.
Return ONLY compact valid JSON with this exact schema:
{{"ok":true|false,"message":"short","display":{{"title":"<=10 chars","body":"<=40 chars"}}}}
No markdown. No explanation.

Facts:
- action: {action}
- request_id: {request_id}
- device_id: {device_id}
- battery: {battery}
- wifi_rssi: {wifi_rssi}

Behavior:
- secretary.memo.capture => warmly acknowledge memo capture
- secretary.reminder.quick_add => warmly acknowledge reminder capture
- secretary.task.next => say you'll help check the next task
- secretary.mode.home => say welcome home and standby
- unknown action => ok=false

Use Japanese. Keep it short enough for a tiny screen.
If unsure, output exactly this fallback JSON:
{fallback}
'''
    proc = subprocess.run(
        ['hermes', 'chat', '-Q', '-q', prompt],
        capture_output=True,
        text=True,
        timeout=MODEL_TIMEOUT,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or f'hermes exit {proc.returncode}')
    text = proc.stdout.strip().splitlines()
    candidate = text[-1].strip() if text else ''
    return json.loads(candidate)


class Handler(BaseHTTPRequestHandler):
    server_version = 'HermesBuddyWebhook/0.1'

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
        if parsed.path != f'/buddy/{TOKEN}':
            self._send_json(404, {'ok': False, 'error': 'not found'})
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
        except ValueError:
            length = 0
        raw = self.rfile.read(length)
        try:
            payload = json.loads(raw.decode('utf-8')) if raw else {}
        except Exception:
            self._send_json(400, {'ok': False, 'message': 'bad json', 'display': {'title': 'ERROR', 'body': 'JSON parse failed'}})
            return

        action = payload.get('request', {}).get('action', '')
        summary = payload_summary(payload)
        sys.stderr.write('payload ' + json.dumps(summary, ensure_ascii=False) + '\n')
        sys.stderr.flush()
        response = fallback_payload(action)
        try:
            candidate = ask_hermes(payload)
            if isinstance(candidate, dict):
                response = candidate
        except Exception as e:
            sys.stderr.write(f'hermes fallback for action={action}: {e}\n')
            sys.stderr.flush()
        if 'display' not in response or not isinstance(response['display'], dict):
            response['display'] = fallback_payload(action)['display']
        if 'message' not in response:
            response['message'] = fallback_payload(action)['message']
        if 'ok' not in response:
            response['ok'] = action in ACTION_COPY
        sys.stderr.write('response ' + json.dumps(response, ensure_ascii=False) + '\n')
        sys.stderr.flush()
        self._send_json(200, response)

    def log_message(self, format, *args):
        sys.stderr.write('%s - - [%s] %s\n' % (self.address_string(), self.log_date_time_string(), format % args))
        sys.stderr.flush()


if __name__ == '__main__':
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f'Hermes Buddy webhook listening on http://{HOST}:{PORT}/buddy/{TOKEN}', flush=True)
    httpd.serve_forever()
