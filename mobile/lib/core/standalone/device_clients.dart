import 'dart:convert';
import 'dart:typed_data';

import 'package:http/http.dart' as http;

class DeviceError implements Exception {
  final String message;
  const DeviceError(this.message);
  @override
  String toString() => message;
}

/// HTTP client for one ESP32 following shared/device-api.md.
class DeviceClient {
  DeviceClient(this.base, {this.key = ''});
  String base;
  String key;
  Map<String, String> get _h => {if (key.isNotEmpty) 'X-Key': key};

  Future<Map<String, dynamic>> getJson(String path) async {
    final r = await http.get(Uri.parse(base + path), headers: _h).timeout(const Duration(seconds: 4));
    return jsonDecode(r.body) as Map<String, dynamic>;
  }

  /// Returns (status, body).
  Future<(int, Map<String, dynamic>)> post(String path, [Map<String, dynamic>? body]) async {
    final r = await http.post(Uri.parse(base + path),
        headers: {..._h, 'content-type': 'application/json'}, body: jsonEncode(body ?? {})).timeout(const Duration(seconds: 8));
    final j = r.body.isEmpty ? <String, dynamic>{} : jsonDecode(r.body);
    return (r.statusCode, j is Map<String, dynamic> ? j : <String, dynamic>{});
  }

  Future<Uint8List> capture({String res = 'svga', int q = 10, bool flash = true, int warmup = 2}) async {
    final r = await http.get(Uri.parse('$base/capture?res=$res&q=$q&flash=${flash ? 1 : 0}&warmup=$warmup'), headers: _h)
        .timeout(const Duration(seconds: 8));
    if (r.statusCode != 200) throw const DeviceError('camera: no frame');
    return r.bodyBytes;
  }

  Future<void> setFlash(bool on) => http.get(Uri.parse('$base/flash?on=${on ? 1 : 0}'), headers: _h);

  /// Stream is on port 81 of the CAM.
  String get streamUrl => '${Uri.parse(base).replace(port: 81, path: '/stream')}';
}
