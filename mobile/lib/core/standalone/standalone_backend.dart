import 'dart:async';
import 'dart:convert';
import 'dart:io' show NetworkInterface, InternetAddressType;
import 'dart:typed_data';

import 'package:flutter/services.dart' show rootBundle;
import 'package:image/image.dart' as img;
import 'package:shared_preferences/shared_preferences.dart';

import '../api/backend.dart';
import '../labels.dart';
import 'classifier.dart';
import 'device_clients.dart';
import 'discovery.dart';
import 'vision.dart';

/// Phone talks to the ESP32 boards directly; YOLO runs on the phone. Same contract as ServerBackend for the UI.
class StandaloneBackend implements Backend {
  StandaloneBackend(this._prefs) {
    _cfg = {
      'tau': null, 'delta': null, 'blur_min': 40.0, 'min_area_frac': 0.015, 'vote_frames': 3,
      'auto_interval_sec': 2.0, 'cooldown_sec': 3.0, 'capture': {'res': 'svga'},
      ...(jsonDecode(_prefs.getString('sa_cfg') ?? '{}') as Map<String, dynamic>),
    };
    _history = (jsonDecode(_prefs.getString('sa_history') ?? '[]') as List).cast<Map<String, dynamic>>();
    _start();
  }

  final SharedPreferences _prefs;
  late Map<String, dynamic> _cfg;
  late List<Map<String, dynamic>> _history;
  final _out = StreamController<TnEvent>.broadcast();
  final _disc = UdpDiscovery();
  final _ref = TrayReference();
  final _yolo = YoloClassifier();
  final _det = YoloDetector();
  double _detConf = 0.3, _detTau = 0.45;
  final Map<String, DeviceClient> _dev = {};
  final Map<String, String> _source = {};
  final Map<String, bool> _online = {};
  double _temperature = 1.0, _tau = 0.45, _delta = 0.1;
  Timer? _poll, _autoTimer;
  bool _auto = false, _busy = false;
  String _state = 'idle';

  // ---- startup ----
  Future<void> _start() async {
    for (final r in ['cam', 'actuator']) {
      final u = _prefs.getString('sa_url_$r');
      if (u != null) _attach(r, u, 'manual');
    }
    await _disc.start();
    _disc.found.listen((e) => _attach(e.$1, e.$2, 'udp'));
    _loadModel();
    _poll = Timer.periodic(const Duration(seconds: 3), (_) => _pollDevices());
    Future.delayed(const Duration(milliseconds: 300), _hello);
  }

  Future<void> _loadModel() async {
    try {
      final m = jsonDecode(await rootBundle.loadString('assets/models/metrics.json')) as Map<String, dynamic>;
      _temperature = (m['temperature'] ?? 1.0).toDouble();
      _tau = (m['tau'] ?? _tau).toDouble();
      _delta = (m['delta'] ?? _delta).toDouble();
      await _yolo.load('assets/models/trashnet-cls.tflite');
    } catch (_) {/* classification model optional */}
    try {
      final m = jsonDecode(await rootBundle.loadString('assets/models/det_metrics.json')) as Map<String, dynamic>;
      _detConf = (m['conf'] ?? _detConf).toDouble();
      _detTau = (m['tau'] ?? _detTau).toDouble();
      await _det.load('assets/models/trashnet-det.tflite');
    } catch (_) {/* detection model missing: falls back to classification */}
  }

  void _attach(String role, String url, String source) {
    final cur = _dev[role];
    if (cur != null && cur.base == url) return;
    _dev[role] = DeviceClient(url, key: _prefs.getString('sa_key_$role') ?? '');
    _source[role] = source;
    _emitDevice(role);
  }

  Map<String, dynamic> _describe(String role) => {'role': role, 'url': _dev[role]?.base, 'online': _online[role] ?? false, 'source': _source[role]};
  void _emitDevice(String role) => _out.add(TnEvent('device', _describe(role)));
  void _hello() => _out.add(TnEvent('hello', {'devices': [_describe('cam'), _describe('actuator')], 'state': _state, 'auto': _auto}));

  Future<void> _pollDevices() async {
    for (final r in ['cam', 'actuator']) {
      final d = _dev[r];
      if (d == null) continue;
      try {
        final s = await d.getJson('/status');
        if (r == 'actuator') _out.add(TnEvent('actuator', {'online': true, ...s}));
        if (_online[r] != true) { _online[r] = true; _emitDevice(r); }
      } catch (_) {
        if (_online[r] != false) { _online[r] = false; _emitDevice(r); }
        if (r == 'actuator') _out.add(const TnEvent('actuator', {'online': false}));
      }
    }
    if (_dev['cam'] == null || _dev['actuator'] == null) {
      // last resort: sweep the /24 of the first known device
      final any = _dev.values.isEmpty ? null : Uri.parse(_dev.values.first.base).host;
      if (any != null) {
        final p = any.substring(0, any.lastIndexOf('.'));
        final f = await UdpDiscovery.scanSubnet(p);
        f.forEach((r, u) => _attach(r, u, 'scan'));
      }
    }
  }

  void _setState(String s) { _state = s; _out.add(TnEvent('sort', {'state': s, 'auto': _auto})); }

  // ---- pipeline ----
  @override
  Future<Map<String, dynamic>> sortCapture() async {
    if (_busy) return {'error': 'busy'};
    _busy = true;
    try {
      return await _runOnce();
    } finally {
      _busy = false;
      _setState('idle');
    }
  }

  /// Detector first; if silent, the white-tray change cue proposes a box and the label falls back to 'other'.
  Future<Det?> _locate(img.Image im, Uint8List jpeg) async {
    final best = bestDet(await _det.detect(jpeg, conf: ((_cfg['det_conf'] ?? _detConf) as num).toDouble()));
    if (best != null || !_ref.ready) return best;
    final box = _ref.find(im, minAreaFrac: ((_cfg['min_area_frac'] ?? 0.015) as num).toDouble(),
        minDiff: ((_cfg['presence_min_diff'] ?? 8) as num).toInt());
    if (box == null) return null;
    return Det(box.x1, box.y1, box.x2, box.y2, 0.0, labelIds.indexOf('other'));
  }

  Future<Map<String, dynamic>> _runDetect(DeviceClient cam, DeviceClient? act) async {
    final res = (_cfg['capture']?['res'] ?? 'svga') as String;
    final n = ((_cfg['vote_frames'] ?? 3) as num).toInt();
    final blurMin = ((_cfg['blur_min'] ?? 40) as num).toDouble();
    _setState('capture');
    final frames = <img.Image>[];
    final dets = <Det?>[];
    final weights = <double>[];
    for (var i = 0; i < n; i++) {
      final b = await cam.capture(res: res, flash: true);
      final im = decode(b);
      if (im == null) continue;
      frames.add(im); weights.add(sharpness(im).clamp(1.0, 1e9)); dets.add(await _locate(im, b));
    }
    if (!dets.any((d) => d != null)) return {'has_object': false};
    final v = voteDets(dets, weights);
    final best = v?.$2 ?? dets.firstWhere((d) => d != null)!;
    final tau = ((_cfg['det_tau'] ?? _detTau) as num).toDouble();
    final uncertain = best.conf < tau;
    final label = uncertain ? 'other' : best.label;
    final fi = dets.indexWhere((d) => d != null);
    final probs = v?.$1 ?? List.generate(labelIds.length, (k) => k == best.cls ? 1.0 : 0.0);
    final result = <String, dynamic>{
      'id': DateTime.now().millisecondsSinceEpoch.toString(), 'ts': DateTime.now().millisecondsSinceEpoch / 1000,
      'has_object': true, 'label': label, 'bin': tnBinById(label).bin, 'confidence': best.conf, 'uncertain': uncertain,
      'blurry': weights.every((w) => w < blurMin), 'box': [best.x1, best.y1, best.x2, best.y2],
      'frame': [frames[fi].width, frames[fi].height], 'mode': 'detect',
      'probs': {for (var i = 0; i < labelIds.length; i++) labelIds[i]: probs[i]},
    };
    await _dispatchResult(result, act);
    return result;
  }

  Future<void> _dispatchResult(Map<String, dynamic> result, DeviceClient? act) async {
    if (act == null) { result['error'] = 'no_actuator'; return; }
    // show the decision at once; the same result (same ts) is sent again when the tray is done
    _out.add(TnEvent('result', {...result, 'dispatch': {'state': 'pending', 'job_id': '', 'ms': 0}}));
    _setState('dispatch');
    final bin = (result['bin'] as num).toInt();
    final t0 = DateTime.now();
    final info = <String, dynamic>{'state': 'sent', 'job_id': '', 'ms': 0};
    result['dispatch'] = info;
    try {
      final st = await act.getJson('/status');
      final flags = st['full'];
      if ((flags is List && bin >= 1 && bin <= flags.length && flags[bin - 1] == true) || (flags == null && bin == 1 && st['bin1_full'] == true)) {
        result['error'] = 'bin_full';
        info['state'] = 'bin_full';
        return;
      }
    } catch (_) {/* status unreadable: let /action decide */}
    final name = tnBinById('${result['label']}').nameVi;
    final deadline = DateTime.now().add(const Duration(seconds: 30));
    var (code, body) = await act.post('/action', {'bin': bin, 'label': result['label'], 'name': name});
    while (code == 409 && body['error'] == 'busy' && DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(seconds: 1));
      (code, body) = await act.post('/action', {'bin': bin, 'label': result['label'], 'name': name});
    }
    if (code == 202) {
      info['job_id'] = '${body['job_id']}';
      final ok = await _waitJob('${body['job_id']}', act);
      info['state'] = ok ? 'done' : 'timeout';
      if (!ok) result['error'] = 'job timeout';
    } else {
      final err = '${body['error'] ?? (code == 409 ? 'busy' : 'actuator_error')}';
      result['error'] = err;
      info['state'] = err == 'busy' ? 'busy' : 'error';
    }
    info['ms'] = DateTime.now().difference(t0).inMilliseconds;
  }

  Future<Map<String, dynamic>> _runOnce() async {
    final cam = _dev['cam'], act = _dev['actuator'];
    if (cam == null) return {'error': 'no_camera'};
    if (_det.ready) {
      final r = await _runDetect(cam, act);
      if (r['has_object'] == true) {
        _remember(r);
        _out.add(TnEvent('result', r));
        _setState('cooldown');
        await Future.delayed(Duration(milliseconds: (((_cfg['cooldown_sec'] ?? 3) as num) * 1000).round()));
      }
      return r;
    }
    if (!_ref.ready) return {'error': 'no_reference'};
    if (!_yolo.ready) return {'error': 'model_not_loaded'};
    final res = (_cfg['capture']?['res'] ?? 'svga') as String;
    _setState('capture');
    final frames = <img.Image>[];
    final bytes = <Uint8List>[];
    final weights = <double>[];
    final n = ((_cfg['vote_frames'] ?? 3) as num).toInt();
    final blurMin = ((_cfg['blur_min'] ?? 40) as num).toDouble();
    for (var i = 0; i < n * 2 && frames.length < n; i++) {
      final b = await cam.capture(res: res, flash: true);
      final im = decode(b);
      if (im == null) continue;
      final sh = sharpness(im);
      if (sh < blurMin && i < n * 2 - 1) continue; // drop blurry frames while retries remain
      frames.add(im); bytes.add(b); weights.add(sh);
    }
    if (frames.isEmpty) return {'error': 'camera_blurry', 'blurry': true};
    final box = _ref.find(frames.first, minAreaFrac: ((_cfg['min_area_frac'] ?? 0.015) as num).toDouble());
    if (box == null) return {'has_object': false};
    _setState('classify');
    final probs = <List<double>>[];
    for (final f in frames) {
      final crop = cropLetterbox(f, box, 224);
      probs.add(applyTemperature(await _yolo.predict(Uint8List.fromList(img.encodeJpg(crop, quality: 92))), _temperature));
    }
    final p = vote(probs, weights);
    final tau = ((_cfg['tau'] ?? _tau) as num).toDouble(), delta = ((_cfg['delta'] ?? _delta) as num).toDouble();
    final d = decide(p, tau, delta);
    final bin = tnBinById(d.label).bin;
    final result = <String, dynamic>{
      'id': DateTime.now().millisecondsSinceEpoch.toString(), 'ts': DateTime.now().millisecondsSinceEpoch / 1000,
      'has_object': true, 'label': d.label, 'bin': bin, 'confidence': d.confidence, 'uncertain': d.uncertain,
      'blurry': weights.every((w) => w < blurMin), 'box': box.list, 'frame': [frames.first.width, frames.first.height],
      'probs': {for (var i = 0; i < labelIds.length; i++) labelIds[i]: d.probs[i]},
    };
    await _dispatchResult(result, act);
    _remember(result);
    _out.add(TnEvent('result', result));
    _setState('cooldown');
    await Future.delayed(Duration(milliseconds: (((_cfg['cooldown_sec'] ?? 3) as num) * 1000).round()));
    return result;
  }

  Future<bool> _waitJob(String id, DeviceClient act) async {
    final end = DateTime.now().add(const Duration(seconds: 40));
    while (DateTime.now().isBefore(end)) {
      await Future.delayed(const Duration(milliseconds: 400));
      try {
        final s = await act.getJson('/status');
        if (s['job_id'] == id && s['job_done'] == true) return true;
        if (s['step'] != null) _out.add(TnEvent('sort', {'state': 'wait', 'auto': _auto, 'step': s['step']}));
      } catch (_) {}
    }
    return false;
  }

  void _remember(Map<String, dynamic> r) {
    _history.insert(0, {'id': r['id'], 'ts': r['ts'], 'label': r['label'], 'confidence': r['confidence'], 'uncertain': r['uncertain'], 'thumb': null});
    if (_history.length > 200) _history.removeRange(200, _history.length);
    _prefs.setString('sa_history', jsonEncode(_history));
  }

  @override
  Future<void> setAuto(bool on) async {
    _auto = on;
    _autoTimer?.cancel();
    if (on) {
      _autoTimer = Timer.periodic(Duration(milliseconds: (((_cfg['auto_interval_sec'] ?? 2) as num) * 1000).round()), (_) async {
        if (_busy || (!_ref.ready && !_det.ready)) return;
        final r = await sortCapture();
        if (r['has_object'] == false) _out.add(TnEvent('result', r));
      });
    }
    _setState(_state);
  }

  @override
  Future<Map<String, dynamic>> setReference() async {
    final cam = _dev['cam'];
    if (cam == null) return {'ok': false, 'error': 'no_camera'};
    final im = decode(await cam.capture(flash: true));
    if (im == null) return {'ok': false, 'error': 'decode'};
    _ref.set(im);
    return {'ok': true};
  }

  // ---- passthroughs ----
  @override
  Stream<TnEvent> events() => _out.stream;
  @override
  Future<List<Map<String, dynamic>>> labels() async => [for (final b in tnBins) {'id': b.id, 'bin': b.bin, 'name_vi': b.nameVi, 'color': b.color.toARGB32()}];
  @override
  Future<void> flash(bool on) async => _dev['cam']?.setFlash(on);
  @override
  Future<Map<String, dynamic>> history(String label) async => {'items': label.isEmpty ? _history : _history.where((r) => r['label'] == label).toList()};
  @override
  Future<Map<String, dynamic>> stats() async {
    final by = <String, int>{};
    final day = <String, int>{};
    var conf = 0.0, unc = 0;
    for (final r in _history) {
      by['${r['label']}'] = (by['${r['label']}'] ?? 0) + 1;
      conf += (r['confidence'] as num).toDouble();
      if (r['uncertain'] == true) unc++;
      final d = DateTime.fromMillisecondsSinceEpoch(((r['ts'] as num) * 1000).round()).toIso8601String().substring(0, 10);
      day[d] = (day[d] ?? 0) + 1;
    }
    final days = day.keys.toList()..sort();
    return {'total': _history.length, 'by_label': by, 'avg_confidence': _history.isEmpty ? 0 : conf / _history.length, 'uncertain': unc,
      'by_day': [for (final d in days.reversed.take(14).toList().reversed) {'day': d, 'n': day[d]}]};
  }

  @override
  Future<void> deleteHistory([String? id]) async {
    id == null ? _history.clear() : _history.removeWhere((r) => r['id'] == id);
    await _prefs.setString('sa_history', jsonEncode(_history));
  }

  @override
  Future<Map<String, dynamic>> settings() async => _cfg;
  @override
  Future<void> patchSettings(Map<String, dynamic> values) async {
    _cfg = {..._cfg, ...values};
    await _prefs.setString('sa_cfg', jsonEncode(_cfg));
  }

  @override
  Future<void> control(Map<String, dynamic> b) async {
    final act = _dev['actuator'];
    if (act == null) throw const ApiError('actuator offline');
    final routes = <String, (String, Map<String, dynamic>?)>{
      'bin': ('/action', {'bin': b['bin']}), 'rotate': ('/rotate', {'bin': b['bin']}), 'dump': ('/servo', {'dump': 1}),
      'lid_open': ('/servo', {'lid': 'open'}), 'lid_close': ('/servo', {'lid': 'close'}), 'servo': ('/servo', {'angle': b['angle']}),
      'lid_angle': ('/servo', {'lid_angle': b['angle']}), 'jog': ('/jog', {'dir': b['dir'], 'ms': b['ms']}),
      'home_set': ('/home/set', null), 'neutral': ('/calib/neutral', {'us': b['us']}),
    };
    final r = routes[b['action']];
    if (r == null) throw const ApiError('unknown action');
    final (code, body) = await act.post(r.$1, r.$2);
    if (code >= 400) throw ApiError('${body['error'] ?? code}');
  }

  @override
  Future<void> rescan() async {
    await _pollDevices();
    final prefixes = <String>{};
    for (final d in _dev.values) {
      final h = Uri.parse(d.base).host;
      if (h.contains('.')) prefixes.add(h.substring(0, h.lastIndexOf('.')));
    }
    try {
      for (final i in await NetworkInterface.list(type: InternetAddressType.IPv4)) {
        for (final a in i.addresses) {
          prefixes.add(a.address.substring(0, a.address.lastIndexOf('.')));
        }
      }
    } catch (_) {}
    for (final p in prefixes) {
      final f = await UdpDiscovery.scanSubnet(p);
      f.forEach((r, u) { _dev.remove(r); _attach(r, u, 'scan'); });
    }
    await _pollDevices();
  }

  @override
  Future<Map<String, dynamic>?> calib() async {
    try { return await _dev['actuator']?.getJson('/calib'); } catch (_) { return null; }
  }

  @override
  Future<void> saveCalib(Map<String, dynamic> c) async {
    final (code, body) = await _dev['actuator']!.post('/calib', c);
    if (code >= 400) throw ApiError('${body['error'] ?? code}');
  }

  @override
  Future<void> manualDevice(String role, String url, String? key) async {
    await _prefs.setString('sa_url_$role', url.trim());
    if (key != null) await _prefs.setString('sa_key_$role', key);
    _dev.remove(role);
    _attach(role, url.trim(), 'manual');
    _dev[role]!.key = key ?? '';
  }

  @override
  String get streamUrl => _dev['cam']?.streamUrl ?? 'http://127.0.0.1:9/none';
  @override
  String? thumbUrl(String? path) => null;
  @override
  Map<String, String> get headers => const {};
  @override
  void dispose() { _poll?.cancel(); _autoTimer?.cancel(); _disc.dispose(); _out.close(); }
}
