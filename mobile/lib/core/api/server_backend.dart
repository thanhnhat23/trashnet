import 'dart:async';
import 'dart:convert';

import 'package:http/http.dart' as http;

import '../labels.dart';
import 'backend.dart';

/// Talks to the PC server REST API (/api/v1) and SSE stream - same contract as the web UI.
class ServerBackend implements Backend {
  ServerBackend(String baseUrl, {this.apiKey = ''}) : base = '${baseUrl.replaceAll(RegExp(r'/+$'), '')}/api/v1';
  final String base;
  final String apiKey;
  final http.Client _c = http.Client();
  http.Client? _sse;

  @override
  Map<String, String> get headers => {if (apiKey.isNotEmpty) 'x-api-key': apiKey};

  Future<Map<String, dynamic>> _call(String method, String path, [Object? body]) async {
    final req = http.Request(method, Uri.parse(base + path))
      ..headers.addAll({...headers, if (body != null) 'content-type': 'application/json'});
    if (body != null) req.body = jsonEncode(body);
    final res = await http.Response.fromStream(await _c.send(req).timeout(const Duration(seconds: 20)));
    final data = res.body.isEmpty ? <String, dynamic>{} : jsonDecode(utf8.decode(res.bodyBytes));
    if (res.statusCode >= 400) throw ApiError((data is Map ? data['detail'] : null)?.toString() ?? res.reasonPhrase ?? 'error');
    return data is Map<String, dynamic> ? data : {'items': data};
  }

  @override
  Stream<TnEvent> events() async* {
    while (true) {
      _sse = http.Client();
      try {
        final req = http.Request('GET', Uri.parse('$base/events'))..headers.addAll(headers);
        final res = await _sse!.send(req);
        String type = 'message';
        await for (final line in res.stream.transform(utf8.decoder).transform(const LineSplitter())) {
          if (line.startsWith('event:')) {
            type = line.substring(6).trim();
          } else if (line.startsWith('data:')) {
            try {
              yield TnEvent(type, jsonDecode(line.substring(5).trim()) as Map<String, dynamic>);
            } catch (_) {}
          }
        }
      } catch (_) {
        yield const TnEvent('offline', {});
      }
      await Future.delayed(const Duration(seconds: 2));
    }
  }

  @override
  Future<List<Map<String, dynamic>>> labels() async => [
        for (final b in tnBins)
          {'id': b.id, 'bin': b.bin, 'name_vi': b.nameVi, 'color': b.color.toARGB32()}
      ];

  @override
  Future<Map<String, dynamic>> sortCapture() => _call('POST', '/sort/capture', {});
  @override
  Future<void> setAuto(bool on) => _call('POST', '/sort/auto', {'enabled': on});
  @override
  Future<Map<String, dynamic>> setReference() => _call('POST', '/camera/reference');
  @override
  Future<void> flash(bool on) => _call('POST', '/camera/flash', {'on': on});
  @override
  Future<void> rescan() => _call('POST', '/devices/rescan');
  @override
  Future<Map<String, dynamic>> history(String label) => _call('GET', '/history?limit=200&label=${Uri.encodeQueryComponent(label)}');
  @override
  Future<Map<String, dynamic>> stats() => _call('GET', '/history/stats');
  @override
  Future<void> deleteHistory([String? id]) => _call('DELETE', id == null ? '/history' : '/history/$id');
  @override
  Future<Map<String, dynamic>> settings() => _call('GET', '/settings');
  @override
  Future<void> patchSettings(Map<String, dynamic> values) => _call('PATCH', '/settings', {'values': values});
  @override
  Future<void> control(Map<String, dynamic> body) => _call('POST', '/actuator/control', body);
  @override
  Future<Map<String, dynamic>?> calib() async {
    try {
      return await _call('GET', '/actuator/calib');
    } catch (_) {
      return null;
    }
  }

  @override
  Future<void> saveCalib(Map<String, dynamic> c) => _call('POST', '/actuator/calib', c);
  @override
  Future<void> manualDevice(String role, String url, String? key) => _call('POST', '/devices/manual', {'role': role, 'url': url, 'key': key});
  @override
  String get streamUrl => '$base/camera/stream';
  @override
  String? thumbUrl(String? path) => path == null ? null : (path.startsWith('http') ? path : base.replaceFirst('/api/v1', '') + path);
  @override
  void dispose() {
    _c.close();
    _sse?.close();
  }
}
