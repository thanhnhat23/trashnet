import 'dart:math' as math;
import 'dart:typed_data';

import 'package:image/image.dart' as img;

class Box {
  final int x1, y1, x2, y2;
  const Box(this.x1, this.y1, this.x2, this.y2);
  List<int> get list => [x1, y1, x2, y2];
}

img.Image? decode(Uint8List b) => img.decodeJpg(b);

/// Variance of a 3x3 Laplacian on a downscaled gray frame (same idea as the server's sharpness gate).
double sharpness(img.Image im) {
  final g = img.grayscale(img.copyResize(im, width: 320));
  final w = g.width, h = g.height;
  double sum = 0, sum2 = 0;
  var n = 0;
  for (var y = 1; y < h - 1; y++) {
    for (var x = 1; x < w - 1; x++) {
      final l = 4 * g.getPixel(x, y).r - g.getPixel(x - 1, y).r - g.getPixel(x + 1, y).r - g.getPixel(x, y - 1).r - g.getPixel(x, y + 1).r;
      sum += l; sum2 += l * l; n++;
    }
  }
  final mean = sum / n;
  return sum2 / n - mean * mean;
}

/// Empty-tray reference used to decide whether an object is present.
class TrayReference {
  List<int>? _g; // 80x60 gray
  static const w = 80, h = 60;
  bool get ready => _g != null;

  List<int> _small(img.Image im) {
    final s = img.grayscale(img.copyResize(im, width: w, height: h, interpolation: img.Interpolation.average));
    return [for (var y = 0; y < h; y++) for (var x = 0; x < w; x++) s.getPixel(x, y).r.toInt()];
  }

  void set(img.Image im) => _g = _small(im);

  /// Bounding box (in original pixel coords) of pixels that differ from the reference, or null.
  Box? find(img.Image im, {double minAreaFrac = 0.015, int minDiff = 22}) {
    final ref = _g;
    if (ref == null) return null;
    final cur = _small(im);
    final diffs = List<int>.generate(cur.length, (i) => cur[i] - ref[i]);
    final sorted = [...diffs]..sort();
    final shift = sorted[sorted.length ~/ 2]; // compensate global exposure change
    final d = diffs.map((v) => (v - shift).abs()).toList();
    final mad = (d.toList()..sort())[d.length ~/ 2]; // median of |diff| ~ noise level
    final noise = 1.4826 * mad;
    final thr = math.max(math.max(minDiff.toDouble(), 4 * noise), minDiff >= 22 ? _otsu(d).toDouble() : 0.0);
    var x1 = w, y1 = h, x2 = -1, y2 = -1, cnt = 0;
    for (var y = 0; y < h; y++) {
      for (var x = 0; x < w; x++) {
        if (d[y * w + x] >= thr) { cnt++; x1 = math.min(x1, x); y1 = math.min(y1, y); x2 = math.max(x2, x); y2 = math.max(y2, y); }
      }
    }
    final frac = cnt / (w * h);
    if (cnt == 0 || frac < minAreaFrac || frac > 0.9) return null;
    final sx = im.width / w, sy = im.height / h;
    return Box((x1 * sx).floor(), (y1 * sy).floor(), ((x2 + 1) * sx).ceil().clamp(0, im.width), ((y2 + 1) * sy).ceil().clamp(0, im.height));
  }

  int _otsu(List<int> v) {
    final hist = List.filled(256, 0);
    for (final e in v) { hist[e.clamp(0, 255)]++; }
    final total = v.length;
    double sum = 0;
    for (var i = 0; i < 256; i++) { sum += i * hist[i]; }
    double sb = 0, best = -1; var wb = 0, thr = 0;
    for (var t = 0; t < 256; t++) {
      wb += hist[t];
      if (wb == 0) continue;
      final wf = total - wb;
      if (wf == 0) break;
      sb += t * hist[t];
      final mb = sb / wb, mf = (sum - sb) / wf;
      final between = wb * wf * (mb - mf) * (mb - mf);
      if (between > best) { best = between; thr = t; }
    }
    return thr;
  }
}

/// Square crop around the box (with padding), letterboxed to [size] with gray 114 like the training data.
img.Image cropLetterbox(img.Image im, Box? box, int size, {double pad = 0.1}) {
  var c = im;
  if (box != null) {
    final bw = box.x2 - box.x1, bh = box.y2 - box.y1;
    final px = (bw * pad).round(), py = (bh * pad).round();
    final x1 = (box.x1 - px).clamp(0, im.width - 1), y1 = (box.y1 - py).clamp(0, im.height - 1);
    final x2 = (box.x2 + px).clamp(x1 + 1, im.width), y2 = (box.y2 + py).clamp(y1 + 1, im.height);
    c = img.copyCrop(im, x: x1, y: y1, width: x2 - x1, height: y2 - y1);
  }
  final s = size / math.max(c.width, c.height);
  final r = img.copyResize(c, width: math.max(1, (c.width * s).round()), height: math.max(1, (c.height * s).round()));
  final out = img.Image(width: size, height: size)..clear(img.ColorRgb8(114, 114, 114));
  img.compositeImage(out, r, dstX: (size - r.width) ~/ 2, dstY: (size - r.height) ~/ 2);
  return out;
}
