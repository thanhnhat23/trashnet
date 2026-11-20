import 'dart:async';
import 'dart:convert';
import 'dart:io';

/// Listens for the devices' UDP beacons `TRASHNET:<CAM|ACTUATOR>:http://ip` on port 5005.
class UdpDiscovery {
  RawDatagramSocket? _s;
  final _c = StreamController<(String role, String url)>.broadcast();
  Stream<(String, String)> get found => _c.stream;

  Future<void> start() async {
    try {
      _s = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 5005, reuseAddress: true, reusePort: false);
      _s!.broadcastEnabled = true;
      _s!.listen((e) {
        if (e != RawSocketEvent.read) return;
        final d = _s!.receive();
        if (d == null) return;
        final p = utf8.decode(d.data, allowMalformed: true).trim().split(':');
        if (p.length >= 3 && p[0] == 'TRASHNET') {
          final role = p[1].toLowerCase() == 'cam' ? 'cam' : 'actuator';
          _c.add((role, p.sublist(2).join(':')));
        }
      });
    } catch (_) {/* port busy: manual entry / subnet scan still work */}
  }

  /// Fallback: probe /status on every host of the phone's /24 subnet.
  static Future<Map<String, String>> scanSubnet(String prefix) async {
    final found = <String, String>{};
    await Future.wait([
      for (var i = 1; i < 255; i++)
        () async {
          final url = 'http://$prefix.$i';
          try {
            final c = HttpClient()..connectionTimeout = const Duration(milliseconds: 350);
            final rq = await c.getUrl(Uri.parse('$url/status'));
            final rs = await rq.close().timeout(const Duration(milliseconds: 800));
            final j = jsonDecode(await rs.transform(utf8.decoder).join()) as Map;
            final role = j['role'];
            if (role == 'cam') found['cam'] = url;
            if (role == 'actuator') found['actuator'] = url;
            c.close(force: true);
          } catch (_) {}
        }()
    ]);
    return found;
  }

  void dispose() { _s?.close(); _c.close(); }
}
