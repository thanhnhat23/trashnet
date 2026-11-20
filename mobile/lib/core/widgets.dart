import 'dart:math' as math;
import 'dart:ui' show FontFeature;

import 'package:flutter/material.dart' hide Stepper;

import 'labels.dart';
import 'theme/app_theme.dart';
import 'theme/tokens.dart';

class SectionTitle extends StatelessWidget {
  const SectionTitle(this.text, {super.key});
  final String text;
  @override
  Widget build(BuildContext context) => Padding(
        padding: EdgeInsets.only(top: tnSpace[4], bottom: tnSpace[1]),
        child: Text(text.toUpperCase(),
            style: TextStyle(fontSize: tnFsSm, fontWeight: FontWeight.w600, letterSpacing: 1, color: paletteOf(context).muted)),
      );
}

class StatusDot extends StatelessWidget {
  const StatusDot(this.kind, this.text, {super.key});
  final String kind; // ok | bad | warn
  final String text;
  @override
  Widget build(BuildContext context) {
    final p = paletteOf(context);
    final c = {'ok': p.ok, 'bad': p.bad, 'warn': p.warn}[kind] ?? p.muted;
    return Row(mainAxisSize: MainAxisSize.min, children: [
      Container(width: 8, height: 8, decoration: BoxDecoration(color: c, shape: BoxShape.circle)),
      const SizedBox(width: 6),
      Text(text, style: TextStyle(fontSize: tnFsSm, color: p.muted)),
    ]);
  }
}

const dispatchText = {
  'sent': 'Đã gửi actuator…', 'done': 'Đã hất xong', 'timeout': 'Actuator không phản hồi', 'busy': 'Actuator đang bận',
  'bin_full': 'Ngăn đầy, chưa hất', 'error': 'Lỗi actuator',
};
const stepText = {
  'rotate': 'quay mâm', 'lid_open': 'mở nắp', 'dump': 'hất rác', 'lid_close': 'đóng nắp', 'measure': 'đo ngăn',
  'return': 'về ngăn 1', 'jog': 'nhích mâm', 'servo': 'chỉnh servo',
};
String stepLabel(String? s) => stepText[s] ?? (s ?? '');

/// Titled bordered block, the same grouping as the web "panel".
class Panel extends StatelessWidget {
  const Panel({super.key, this.title, required this.children});
  final String? title;
  final List<Widget> children;
  @override
  Widget build(BuildContext context) {
    final p = paletteOf(context);
    return Container(
      width: double.infinity,
      margin: EdgeInsets.only(bottom: tnSpace[2]),
      padding: EdgeInsets.symmetric(horizontal: tnSpace[3], vertical: tnSpace[2]),
      decoration: BoxDecoration(color: p.surface, border: Border.all(color: p.line), borderRadius: BorderRadius.circular(tnRadius)),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        if (title != null)
          Padding(
            padding: EdgeInsets.only(bottom: tnSpace[1]),
            child: Text(title!.toUpperCase(), style: TextStyle(fontSize: tnFsXs, fontWeight: FontWeight.w600, letterSpacing: 1, color: p.muted)),
          ),
        ...children,
      ]),
    );
  }
}

/// Name · bin on the left, one confidence number on the right, small notes below.
/// Result: winner + confidence, how far ahead of the runner-up, the model's score for every bin, tray progress.
class ResultCard extends StatelessWidget {
  const ResultCard(this.r, {super.key, this.step});
  final Map<String, dynamic>? r;
  final String? step;
  @override
  Widget build(BuildContext context) {
    final p = paletteOf(context);
    if (r == null) return Text('Chưa có kết quả.', style: TextStyle(color: p.muted));
    if (r!['has_object'] == false) return Text('Khay trống', style: TextStyle(fontSize: tnFsLg, fontWeight: FontWeight.w600, color: p.muted));
    final b = tnBinById('${r!['label']}');
    final conf = ((r!['confidence'] ?? 0) * 100).round();
    final sc = Map<String, dynamic>.from(r!['scores'] ?? r!['probs'] ?? {});
    final rows = [for (final x in tnBins) (bin: x, v: ((sc[x.id] ?? 0) as num).toDouble())]..sort((a, c) => c.v.compareTo(a.v));
    final margin = ((r!['margin'] ?? (rows[0].v - rows[1].v)) as num).toDouble();
    final (Color vc, String verdict) = r!['uncertain'] == true
        ? (p.bad, 'Không chắc → bỏ vào Rác khác')
        : margin >= 0.3 ? (p.ok, 'Chắc chắn') : margin >= 0.1 ? (p.warn, 'Khá chắc') : (p.bad, 'Phân vân');
    final gap = rows[1].v > 0 ? ' · hơn ${rows[1].bin.nameVi} ${(margin * 100).round()} điểm' : '';
    String pct(double v) => v > 0 && v < 0.01 ? '<1%' : '${(v * 100).round()}%';

    final d = r!['dispatch'] as Map?;
    final state = d?['state'] as String?;
    Widget? disp;
    if (state == 'pending' || state == 'sent') {
      final s = step != null && step != 'idle' ? stepLabel(step!) : null;
      disp = Row(children: [
        SizedBox(width: 12, height: 12, child: CircularProgressIndicator(strokeWidth: 2, color: p.warn)),
        const SizedBox(width: 8),
        Text(s != null ? 'Đang $s…' : 'Đang gửi tới ngăn ${r!['bin']}…', style: TextStyle(fontSize: tnFsSm, color: p.warn)),
      ]);
    } else if (state != null) {
      final ok = state == 'done';
      final t = ok ? '✓ Đã hất vào ngăn ${r!['bin']} · ${((d!['ms'] ?? 0) / 1000).toStringAsFixed(1)} s' : '! ${dispatchText[state] ?? state}';
      disp = Text(t, style: TextStyle(fontSize: tnFsSm, color: ok ? p.ok : p.bad));
    }

    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(crossAxisAlignment: CrossAxisAlignment.baseline, textBaseline: TextBaseline.alphabetic, children: [
        Text(b.nameVi, style: TextStyle(fontSize: tnFsLg, fontWeight: FontWeight.w600, color: b.color)),
        const SizedBox(width: 8),
        Expanded(child: Text('Ngăn ${r!['bin'] ?? b.bin}', style: TextStyle(fontSize: tnFsSm, color: p.muted))),
        Text('$conf%', style: TextStyle(fontSize: tnFsXl, fontWeight: FontWeight.w600, color: b.color, fontFeatures: const [FontFeature.tabularFigures()])),
      ]),
      Text(verdict + gap + (r!['blurry'] == true ? ' · ảnh hơi mờ' : ''), style: TextStyle(fontSize: tnFsSm, color: vc, fontWeight: FontWeight.w500)),
      const SizedBox(height: 10),
      for (final x in rows)
        Padding(
          padding: const EdgeInsets.symmetric(vertical: 3),
          child: Row(children: [
            SizedBox(width: 110, child: Text('${x.bin.bin}  ${x.bin.nameVi}', style: TextStyle(
                fontSize: tnFsSm, fontWeight: x.bin.id == rows.first.bin.id ? FontWeight.w600 : FontWeight.normal,
                color: x.bin.id == rows.first.bin.id ? null : p.muted))),
            Expanded(child: ClipRRect(
              borderRadius: BorderRadius.circular(4),
              child: LinearProgressIndicator(value: x.v.clamp(0.0, 1.0), minHeight: 8, backgroundColor: p.line,
                  color: x.bin.id == rows.first.bin.id ? x.bin.color : x.bin.color.withOpacity(0.55)),
            )),
            SizedBox(width: 44, child: Text(pct(x.v), textAlign: TextAlign.right, style: TextStyle(fontSize: tnFsSm, fontFamily: 'monospace'))),
          ]),
        ),
      if (disp != null) ...[Divider(color: p.line, height: 20), disp],
    ]);
  }
}

class Stepper extends StatefulWidget {
  const Stepper({super.key, required this.value, required this.step, required this.onChanged});
  final num value, step;
  final ValueChanged<num> onChanged;
  @override
  State<Stepper> createState() => _StepperState();
}

class _StepperState extends State<Stepper> {
  late num v = widget.value;
  void _set(num n) { setState(() => v = n); widget.onChanged(n); }
  @override
  Widget build(BuildContext context) => Row(mainAxisSize: MainAxisSize.min, children: [
        OutlinedButton(onPressed: () => _set(v - widget.step), child: const Text('−')),
        SizedBox(width: 64, child: Center(child: Text(v == v.roundToDouble() ? '${v.toInt()}' : v.toStringAsFixed(1)))),
        OutlinedButton(onPressed: () => _set(v + widget.step), child: const Text('+')),
      ]);
}

class BarRow extends StatelessWidget {
  const BarRow(this.label, this.value, this.max, {super.key, this.color});
  final String label;
  final int value, max;
  final Color? color;
  @override
  Widget build(BuildContext context) {
    final p = paletteOf(context);
    return Padding(
      padding: EdgeInsets.symmetric(vertical: tnSpace[0] + 2),
      child: Row(children: [
        SizedBox(width: 90, child: Text(label)),
        Expanded(
            child: ClipRRect(
                borderRadius: BorderRadius.circular(4),
                child: LinearProgressIndicator(value: max == 0 ? 0 : value / max, minHeight: 8, backgroundColor: p.line, color: color ?? p.text))),
        SizedBox(width: 48, child: Text('$value', textAlign: TextAlign.right, style: const TextStyle(fontFamily: 'monospace'))),
      ]),
    );
  }
}

void toast(BuildContext c, String msg) =>
    ScaffoldMessenger.of(c)..hideCurrentSnackBar()..showSnackBar(SnackBar(content: Text(msg), duration: const Duration(seconds: 3)));

/// Run an async action; show errors as toast.
Future<void> guarded(BuildContext c, Future<void> Function() fn) async {
  try {
    await fn();
  } catch (e) {
    if (c.mounted) toast(c, '$e');
  }
}

Widget labelled(String label, Widget child) => Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(children: [Expanded(child: Text(label)), child]),
    );

/// Number tile: small label, big value.
class StatTile extends StatelessWidget {
  const StatTile(this.label, this.value, {super.key, this.hint});
  final String label, value;
  final String? hint;
  @override
  Widget build(BuildContext context) {
    final p = paletteOf(context);
    return Container(
      padding: EdgeInsets.all(tnSpace[2] + 4),
      decoration: BoxDecoration(color: p.surface, border: Border.all(color: p.line), borderRadius: BorderRadius.circular(tnRadius)),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(label, style: TextStyle(fontSize: tnFsSm, color: p.muted)),
        Text(value, style: const TextStyle(fontSize: tnFsXl, fontWeight: FontWeight.w600)),
        if (hint != null) Text(hint!, style: TextStyle(fontSize: tnFsSm, color: p.muted)),
      ]),
    );
  }
}

class _DonutPainter extends CustomPainter {
  _DonutPainter(this.values, this.colors, this.track);
  final List<int> values;
  final List<Color> colors;
  final Color track;
  @override
  void paint(Canvas canvas, Size size) {
    final r = size.shortestSide / 2 - 8;
    final rect = Rect.fromCircle(center: size.center(Offset.zero), radius: r);
    final base = Paint()..style = PaintingStyle.stroke..strokeWidth = 16..color = track;
    canvas.drawCircle(rect.center, r, base);
    final total = values.fold<int>(0, (a, b) => a + b);
    if (total == 0) return;
    var start = -math.pi / 2;
    for (var i = 0; i < values.length; i++) {
      if (values[i] == 0) continue;
      final sweep = values[i] / total * 2 * math.pi;
      final gap = values.where((v) => v > 0).length > 1 ? 0.04 : 0.0;
      canvas.drawArc(rect, start, math.max(0.0, sweep - gap), false, Paint()..style = PaintingStyle.stroke..strokeWidth = 16..strokeCap = StrokeCap.butt..color = colors[i]);
      start += sweep;
    }
  }

  @override
  bool shouldRepaint(_DonutPainter o) => o.values != values;
}

/// Share by bin: donut with the total in the middle, legend with values beside it.
class Donut extends StatelessWidget {
  const Donut(this.items, {super.key});
  final List<({String label, int value, Color color})> items;
  @override
  Widget build(BuildContext context) {
    final p = paletteOf(context);
    final total = items.fold<int>(0, (a, i) => a + i.value);
    return Row(children: [
      SizedBox(
        width: 140, height: 140,
        child: Stack(alignment: Alignment.center, children: [
          CustomPaint(size: const Size(140, 140), painter: _DonutPainter([for (final i in items) i.value], [for (final i in items) i.color], p.line)),
          Column(mainAxisSize: MainAxisSize.min, children: [
            Text('$total', style: const TextStyle(fontSize: 26, fontWeight: FontWeight.w600)),
            Text('tổng', style: TextStyle(fontSize: 11, color: p.muted)),
          ]),
        ]),
      ),
      const SizedBox(width: 20),
      Expanded(child: Column(children: [
        for (final i in items)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 4),
            child: Row(children: [
              Container(width: 10, height: 10, decoration: BoxDecoration(color: i.color, shape: BoxShape.circle)),
              const SizedBox(width: 8),
              Expanded(child: Text(i.label)),
              Text('${i.value}', style: const TextStyle(fontWeight: FontWeight.bold, fontFamily: 'monospace')),
            ]),
          ),
      ])),
    ]);
  }
}

class _ColumnsPainter extends CustomPainter {
  _ColumnsPainter(this.items, this.ink, this.grid, this.muted);
  final List<(String, int)> items;
  final Color ink, grid, muted;
  @override
  void paint(Canvas canvas, Size size) {
    const padL = 24.0, padB = 18.0, padT = 14.0;
    final h = size.height - padB - padT;
    final maxV = items.fold<int>(1, (a, i) => math.max(a, i.$2));
    final top = ((maxV / 4).ceil() * 4).clamp(4, 1 << 30);
    TextPainter tp(String t, Color c, {double fs = 10, FontWeight w = FontWeight.normal}) =>
        TextPainter(text: TextSpan(text: t, style: TextStyle(fontSize: fs, color: c, fontWeight: w)), textDirection: TextDirection.ltr)..layout();
    for (var g = 0; g <= 4; g++) {
      final y = padT + h - g / 4 * h;
      canvas.drawLine(Offset(padL, y), Offset(size.width, y), Paint()..color = grid);
      final t = tp('${(g / 4 * top).round()}', muted);
      t.paint(canvas, Offset(padL - 6 - t.width, y - t.height / 2));
    }
    final n = items.length;
    final step = (size.width - padL) / math.max(n, 1);
    final bw = math.min(26.0, step * 0.6);
    final peak = items.indexWhere((i) => i.$2 == maxV && maxV > 0);
    for (var k = 0; k < n; k++) {
      final x = padL + k * step + (step - bw) / 2;
      final bh = items[k].$2 / top * h;
      final rr = RRect.fromRectAndRadius(Rect.fromLTWH(x, padT + h - math.max(bh, 1), bw, math.max(bh, 1)), const Radius.circular(4));
      canvas.drawRRect(rr, Paint()..color = ink.withOpacity(0.85));
      if (k == peak) {
        final t = tp('${items[k].$2}', ink, fs: 11, w: FontWeight.w600);
        t.paint(canvas, Offset(x + bw / 2 - t.width / 2, padT + h - bh - t.height - 2));
      }
      if (n <= 7 || k % 2 == 0) {
        final t = tp(items[k].$1, muted);
        t.paint(canvas, Offset(x + bw / 2 - t.width / 2, padT + h + 4));
      }
    }
  }

  @override
  bool shouldRepaint(_ColumnsPainter o) => o.items != items;
}

/// Columns over time: thin rounded columns, faint grid, only the maximum labelled.
class Columns extends StatelessWidget {
  const Columns(this.items, {super.key});
  final List<(String, int)> items;
  @override
  Widget build(BuildContext context) {
    final p = paletteOf(context);
    return SizedBox(height: 170, width: double.infinity, child: CustomPaint(painter: _ColumnsPainter(items, p.text, p.line, p.muted)));
  }
}

/// Round bin button: number in a coloured ring, bin name below.
class BinDot extends StatelessWidget {
  const BinDot(this.bin, {super.key, required this.active, required this.onTap});
  final TnBin bin;
  final bool active;
  final VoidCallback? onTap;
  @override
  Widget build(BuildContext context) => InkResponse(
        onTap: onTap,
        radius: 56,
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          Container(
            width: 92, height: 92,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              border: Border.all(color: bin.color, width: 3),
              color: active ? bin.color : Colors.transparent,
            ),
            child: Text('${bin.bin}', style: TextStyle(fontSize: 32, fontWeight: FontWeight.w600, color: active ? Colors.white : bin.color)),
          ),
          const SizedBox(height: 6),
          Text(bin.nameVi, style: const TextStyle(fontWeight: FontWeight.w600)),
        ]),
      );
}
