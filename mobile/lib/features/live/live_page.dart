import 'package:flutter/material.dart';

import '../../core/api/mjpeg.dart';
import '../../core/labels.dart';
import '../../core/state/app_state.dart';
import '../../core/theme/app_theme.dart';
import '../../core/widgets.dart';

class LivePage extends StatefulWidget {
  const LivePage({super.key});
  @override
  State<LivePage> createState() => _LivePageState();
}

class _LivePageState extends State<LivePage> {
  bool flashOn = false, busy = false;
  String tab = 'auto';
  int running = 0;
  DateTime runStart = DateTime.now();

  Future<void> _run(Future<void> Function() fn) async {
    setState(() => busy = true);
    await guarded(context, fn);
    if (mounted) setState(() => busy = false);
  }

  @override
  Widget build(BuildContext context) => ListenableBuilder(
        listenable: appState,
        builder: (context, _) {
          final b = appState.backend!;
          final auto = appState.sort['auto'] == true;
          final idle = appState.sort['state'] == 'idle';
          final r = appState.result;
          final down = [
            for (final r in ['cam', 'actuator']) if (appState.devices[r]?['online'] != true) r == 'cam' ? 'ESP32-CAM' : 'Actuator'
          ];
          final fullBins = [
            for (final e in ((appState.actuator?['full'] as List?) ?? const []).asMap().entries) if (e.value == true) e.key + 1
          ];
          final stage = AspectRatio(
            aspectRatio: 4 / 3,
            child: ClipRRect(
              borderRadius: BorderRadius.circular(8),
              child: Container(
                color: Colors.black,
                child: LayoutBuilder(builder: (_, c) => Stack(children: [
                  MjpegView(url: b.streamUrl, headers: b.headers),
                  if (r?['box'] != null && r?['frame'] != null) _bbox(r!, c.maxWidth, c.maxHeight),
                ])),
              ),
            ),
          );
          final a = appState.actuator;
          final pos = (a?['position'] as num?)?.toInt();
          final step = appState.sort['step'] as String?;
          final lost = a?['tray_lost'] == true;
          if (running != 0 && a?['state'] == 'idle' && DateTime.now().difference(runStart).inMilliseconds > 1500) running = 0;
          return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            if (down.isNotEmpty)
              _alert(Row(children: [
                Expanded(child: StatusDot('bad', '${down.join(', ')} chưa kết nối')),
                FilledButton(
                  onPressed: busy ? null : () => _run(() async {
                    await b.rescan();
                    if (mounted) toast(context, 'Đã quét lại mạng');
                  }),
                  child: const Text('Kết nối lại'),
                ),
              ])),
            if (fullBins.isNotEmpty) _alert(StatusDot('bad', 'Ngăn đầy: ${fullBins.join(', ')}. Hãy đổ rác trước khi phân loại tiếp.')),
            if (lost) _alert(const StatusDot('warn', 'Mất vị trí mâm: chỉnh về ngăn 1 rồi bấm "Đặt làm Home" ở tab Thiết bị.')),
            stage,
            const SizedBox(height: 8),
            SizedBox(
              width: double.infinity,
              child: SegmentedButton<String>(
                showSelectedIcon: false,
                segments: const [ButtonSegment(value: 'auto', label: Text('Tự động')), ButtonSegment(value: 'remote', label: Text('Điều khiển'))],
                selected: {tab},
                onSelectionChanged: (v) => setState(() => tab = v.first),
              ),
            ),
            if (tab == 'auto') ...[
            Panel(children: [ResultCard(r, step: step)]),
            const SizedBox(height: 4),
            StatusDot(idle ? 'ok' : 'warn', idle ? 'Sẵn sàng' : 'Đang xử lý: ${step != null ? stepLabel(step) : appState.sort['state']}'),
            const SizedBox(height: 8),
            SizedBox(
              width: double.infinity,
              height: 52,
              child: FilledButton(
                onPressed: busy ? null : () => _run(() async {
                  final res = await b.sortCapture();
                  if (res['error'] == 'no_reference') {
                    if (mounted) toast(context, 'Hãy bấm "Đặt khay trống" khi khay chưa có rác.');
                  } else if (res['error'] == 'bin_full') {
                    if (mounted) toast(context, 'Ngăn đầy: hãy đổ rác trước');
                  } else if (res['error'] != null && mounted) {
                    toast(context, '${res['error']}');
                  }
                  appState.setResult(res);
                }),
                child: const Text('Chụp & phân loại', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600)),
              ),
            ),
            const SizedBox(height: 8),
            Panel(children: [
              SwitchListTile(
                contentPadding: EdgeInsets.zero, dense: true, title: const Text('Tự động phân loại'), value: auto,
                onChanged: (v) => _run(() => b.setAuto(v)),
              ),
              SwitchListTile(
                contentPadding: EdgeInsets.zero, dense: true, title: const Text('Đèn flash'), value: flashOn,
                onChanged: (v) => _run(() async { flashOn = v; await b.flash(v); }),
              ),
              Align(
                alignment: Alignment.centerLeft,
                child: OutlinedButton(
                  onPressed: busy ? null : () => _run(() async {
                    final res = await b.setReference();
                    if (mounted) toast(context, res['ok'] == true ? 'Đã lưu ảnh khay trống' : '${res['error']}');
                  }),
                  child: const Text('Đặt khay trống'),
                ),
              ),
            ]),
            ] else ...[
              const SizedBox(height: 16),
              Center(
                child: Wrap(spacing: 36, runSpacing: 20, alignment: WrapAlignment.center, children: [
                  for (final bn in tnBins)
                    BinDot(bn, active: running == bn.bin, onTap: () async {
                      if (running != 0 || !idle) { toast(context, 'Đang chạy, chờ xong rồi bấm tiếp'); return; }
                      setState(() { running = bn.bin; runStart = DateTime.now(); });
                      try {
                        await b.control({'action': 'bin', 'bin': bn.bin});
                      } catch (e) {
                        if (mounted) { setState(() => running = 0); toast(context, '$e'); }
                      }
                    }),
                ]),
              ),
              const SizedBox(height: 12),
              Center(child: Text(
                (a?['state'] != null && a?['state'] != 'idle') ? 'Đang ${(a?['step'] != null && a?['step'] != 'idle') ? stepLabel('${a?['step']}') : 'chạy'}…' : 'Chạm vào loại rác để chạy cả kịch bản',
                style: TextStyle(color: paletteOf(context).muted),
              )),
            ],
          ]);
        },
      );

  Widget _alert(Widget child) => Container(
        width: double.infinity,
        margin: const EdgeInsets.only(bottom: 8),
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
        decoration: BoxDecoration(border: Border.all(color: paletteOf(context).bad), borderRadius: BorderRadius.circular(8)),
        child: child,
      );

  Widget _bbox(Map<String, dynamic> r, double w, double h) {
    final box = (r['box'] as List).map((e) => (e as num).toDouble()).toList();
    final fr = (r['frame'] as List).map((e) => (e as num).toDouble()).toList();
    final scale = (fr[0] / fr[1]) > (w / h) ? w / fr[0] : h / fr[1];
    final ox = (w - fr[0] * scale) / 2, oy = (h - fr[1] * scale) / 2;
    return Positioned(
      left: ox + box[0] * scale, top: oy + box[1] * scale,
      width: (box[2] - box[0]) * scale, height: (box[3] - box[1]) * scale,
      child: IgnorePointer(child: Container(decoration: BoxDecoration(border: Border.all(color: tnBinById('${r['label']}').color, width: 2)))),
    );
  }
}
