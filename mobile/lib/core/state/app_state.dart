import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../api/backend.dart';
import '../api/server_backend.dart';
import '../standalone/standalone_backend.dart';

enum Mode { server, standalone }

/// Observable app state, equivalent to web `state/store.js`.
class AppState extends ChangeNotifier {
  Backend? backend;
  Mode mode = Mode.standalone;
  String serverUrl = '';
  String apiKey = '';
  String theme = '';   // '', light, dark
  bool connected = false;
  Map<String, Map<String, dynamic>> devices = {};
  Map<String, dynamic> sort = {'state': 'idle', 'auto': false};
  Map<String, dynamic>? actuator;
  Map<String, dynamic>? result;
  StreamSubscription<TnEvent>? _sub;
  late SharedPreferences _prefs;

  Future<void> init() async {
    _prefs = await SharedPreferences.getInstance();
    mode = _prefs.getString('mode') == 'server' ? Mode.server : Mode.standalone;
    serverUrl = _prefs.getString('server_url') ?? '';
    apiKey = _prefs.getString('api_key') ?? '';
    theme = _prefs.getString('theme') ?? '';
    await connect();
  }

  Future<void> connect() async {
    await _sub?.cancel();
    backend?.dispose();
    devices = {};
    connected = false;
    backend = (mode == Mode.server && serverUrl.isNotEmpty)
        ? ServerBackend(serverUrl, apiKey: apiKey)
        : StandaloneBackend(_prefs);
    _sub = backend!.events().listen(_onEvent);
    notifyListeners();
  }

  void _onEvent(TnEvent e) {
    final d = e.data;
    switch (e.type) {
      case 'hello':
        connected = true;
        devices = {for (final x in (d['devices'] as List? ?? [])) (x['role'] as String): Map<String, dynamic>.from(x as Map)};
        sort = {'state': d['state'] ?? 'idle', 'auto': d['auto'] ?? false};
      case 'device':
        devices = {...devices, d['role'] as String: d};
      case 'sort':
        sort = d;
      case 'result':
        result = d;
      case 'actuator':
        actuator = d;
      case 'offline':
        connected = false;
    }
    notifyListeners();
  }

  Future<void> saveConnection({required Mode newMode, required String url, required String key}) async {
    mode = newMode; serverUrl = url.trim(); apiKey = key.trim();
    await _prefs.setString('mode', mode.name);
    await _prefs.setString('server_url', serverUrl);
    await _prefs.setString('api_key', apiKey);
    await connect();
  }

  void setResult(Map<String, dynamic> r) {
    result = r;
    notifyListeners();
  }

  Future<void> setTheme(String t) async {
    theme = t;
    await _prefs.setString('theme', t);
    notifyListeners();
  }
}

final appState = AppState();
