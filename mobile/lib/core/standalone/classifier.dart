import 'dart:math' as math;
import 'dart:typed_data';

import 'package:ultralytics_yolo/ultralytics_yolo.dart';

/// Class order shared with the server / training: paper, glass, other, plastic.
const labelIds = ['paper', 'glass', 'other', 'plastic'];

class Decision {
  final String label;
  final double confidence;
  final bool uncertain;
  final List<double> probs;
  const Decision(this.label, this.confidence, this.uncertain, this.probs);
}

/// p^(1/T), renormalised.
List<double> applyTemperature(List<double> p, double t) {
  final q = p.map((v) => math.pow(math.max(v, 1e-9), 1 / t).toDouble()).toList();
  final s = q.fold(0.0, (a, b) => a + b);
  return q.map((v) => v / s).toList();
}

/// Weighted average of per-frame probabilities (weights = sharpness).
List<double> vote(List<List<double>> ps, List<double> w) {
  final ws = w.fold(0.0, (a, b) => a + b);
  final out = List.filled(ps.first.length, 0.0);
  for (var i = 0; i < ps.length; i++) {
    for (var k = 0; k < out.length; k++) { out[k] += ps[i][k] * w[i] / (ws == 0 ? 1 : ws); }
  }
  return out;
}

/// Reject -> "other" when the top class is weak or too close to the runner-up.
Decision decide(List<double> p, double tau, double delta) {
  final idx = List.generate(p.length, (i) => i)..sort((a, b) => p[b].compareTo(p[a]));
  final top = p[idx[0]], second = p[idx[1]];
  final reject = top < tau || (top - second) < delta;
  return Decision(reject ? 'other' : labelIds[idx[0]], top, reject, p);
}

/// On-device YOLO11n-cls (TFLite) through the ultralytics_yolo plugin.
/// NOTE: the plugin API changes between versions - verify `predict` result parsing on a real device.
class YoloClassifier {
  YOLO? _yolo;
  Future<void> load(String modelAsset) async {
    _yolo = YOLO(modelPath: modelAsset, task: YOLOTask.classify);
    await _yolo!.loadModel();
  }

  bool get ready => _yolo != null;

  /// Returns probabilities in [labelIds] order.
  Future<List<double>> predict(Uint8List jpeg) async {
    final res = await _yolo!.predict(jpeg);
    final out = List.filled(labelIds.length, 0.0);
    final cls = res['classification'] ?? res['classifications'] ?? res['detections'];
    if (cls is List) {
      for (final c in cls) {
        final name = '${(c as Map)['className'] ?? c['name'] ?? c['label']}';
        final i = labelIds.indexOf(name);
        if (i >= 0) out[i] = ((c['confidence'] ?? c['score'] ?? 0) as num).toDouble();
      }
    } else if (cls is Map) {
      final name = '${cls['className'] ?? cls['name']}';
      final i = labelIds.indexOf(name);
      if (i >= 0) out[i] = ((cls['confidence'] ?? 0) as num).toDouble();
    }
    final s = out.fold(0.0, (a, b) => a + b);
    return s > 0 ? out.map((v) => v / s).toList() : out;
  }
}


class Det {
  final int x1, y1, x2, y2;
  final double conf;
  final int cls; // index in labelIds
  const Det(this.x1, this.y1, this.x2, this.y2, this.conf, this.cls);
  int get area => (x2 - x1).clamp(0, 1 << 30) * (y2 - y1).clamp(0, 1 << 30);
  String get label => labelIds[cls];
}

Det? bestDet(List<Det> ds) => ds.isEmpty ? null : (ds.toList()..sort((a, b) => b.conf.compareTo(a.conf))).first;

/// Weighted class vote over frames; returns (class probabilities, best box of the winning class) or null.
(List<double>, Det)? voteDets(List<Det?> per, List<double> w) {
  final score = List.filled(labelIds.length, 0.0);
  for (var i = 0; i < per.length; i++) { final d = per[i]; if (d != null) score[d.cls] += d.conf * w[i]; }
  final total = score.fold(0.0, (a, b) => a + b);
  if (total <= 0) return null;
  var win = 0;
  for (var k = 1; k < score.length; k++) { if (score[k] > score[win]) win = k; }
  final c = per.whereType<Det>().where((d) => d.cls == win).toList();
  final best = (c..sort((a, b) => b.conf.compareTo(a.conf))).first;
  final agree = c.length / per.whereType<Det>().length;
  final mean = c.fold(0.0, (a, d) => a + d.conf) / c.length;
  return (score.map((v) => v / total).toList(), Det(best.x1, best.y1, best.x2, best.y2, mean * (0.6 + 0.4 * agree), win));
}

/// On-device YOLO11n detection (TFLite) through ultralytics_yolo.
/// NOTE: verify the result keys on a real device; the plugin changes between versions.
class YoloDetector {
  YOLO? _yolo;
  Future<void> load(String modelAsset) async {
    _yolo = YOLO(modelPath: modelAsset, task: YOLOTask.detect);
    await _yolo!.loadModel();
  }

  bool get ready => _yolo != null;

  Future<List<Det>> detect(Uint8List jpeg, {double conf = 0.3}) async {
    final res = await _yolo!.predict(jpeg, confidenceThreshold: conf);
    final out = <Det>[];
    for (final b in (res['boxes'] ?? res['detections'] ?? []) as List) {
      final m = b as Map;
      final name = '${m['className'] ?? m['class'] ?? m['name']}';
      final k = labelIds.indexOf(name);
      if (k < 0) continue;
      final c = ((m['confidence'] ?? m['score'] ?? 0) as num).toDouble();
      if (c < conf) continue;
      final bb = m['bbox'] is List ? (m['bbox'] as List).map((e) => (e as num).toInt()).toList() : null;
      final x1 = bb?[0] ?? (m['x1'] as num).toInt(), y1 = bb?[1] ?? (m['y1'] as num).toInt();
      final x2 = bb?[2] ?? (m['x2'] as num).toInt(), y2 = bb?[3] ?? (m['y2'] as num).toInt();
      out.add(Det(x1, y1, x2, y2, c, k));
    }
    return out;
  }
}
