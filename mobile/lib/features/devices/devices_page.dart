import 'package:flutter/material.dart' hide Stepper;

import '../../core/state/app_state.dart';
import '../../core/theme/app_theme.dart';
import '../../core/widgets.dart';

const _groups = <(String, List<(String, String, num)>)>[
  ('Mâm quay', [
    ('neutral_us', 'Xung đứng yên (µs)', 1),
    ('delta_us', 'Biên độ quay Δ (µs)', 5),
    ('t90_fwd_ms', 'Quay 90° thuận (ms)', 10),
    ('t90_rev_ms', 'Quay 90° nghịch (ms)', 10),
    ('t180_fwd_ms', 'Quay 180° thuận (ms)', 10),
    ('t180_rev_ms', 'Quay 180° nghịch (ms)', 10),
    ('ramp_ms', 'Tăng/giảm tốc (ms)', 10),
    ('brake_ms', 'Phanh (ms)', 5),
    ('brake_us', 'Lực phanh (µs)', 5),
  ]),
  ('Servo hất', [
    ('dump_rest', 'Góc nghỉ', 1),
    ('dump_target', 'Góc hất', 1),
    ('dump_fwd_ms', 'Thời gian mở (ms)', 50),
    ('dump_hold_ms', 'Giữ mở (ms)', 100),
    ('dump_rev_ms', 'Thời gian đóng (ms)', 50),
  ]),
  ('Nắp', [
    ('lid_rest', 'Góc nắp đóng', 1),
    ('lid_open', 'Góc nắp mở', 1),
    ('lid_settle_ms', 'Chờ nắp mở (ms)', 50),
  ]),
  ('Cảm biến', [
    ('full_cm', 'CB1: ngưỡng đầy (cm)', 0.5),
    ('lid_cm', 'CB2: ngưỡng mở nắp (cm)', 0.5),
  ]),
];

class DevicesPage extends StatefulWidget {
  const DevicesPage({super.key});
  @override
  State<DevicesPage> createState() => _DevicesPageState();
}

class _DevicesPageState extends State<DevicesPage> {
  Map<String, dynamic>? calib;
  bool calibLoaded = false;
  final ctl = {for (final r in ['cam', 'actuator']) r: [TextEditingController(), TextEditingController()]};

  @override
  void initState() {
    super.initState();
    appState.backend!.calib().then((c) { if (mounted) setState(() { calib = c; calibLoaded = true; }); });
  }

  Future<void> _act(Map<String, dynamic> body, [String? msg]) => guarded(context, () async {
        await appState.backend!.control(body);
        if (msg != null && mounted) toast(context, msg);
      });

  Widget _deviceRow(String role, String name) {
    final d = appState.devices[role];
    final p = paletteOf(context);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Row(children: [
        Expanded(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          StatusDot(d?['online'] == true ? 'ok' : 'bad', name),
          Text('${d?['url'] ?? 'chưa tìm thấy'}', style: TextStyle(fontSize: 13, color: p.muted, fontFamily: 'monospace')),
        ])),
        Text(d?['source'] ?? '—', style: TextStyle(fontSize: 11, color: p.muted)),
      ]),
    );
  }

  Widget _manual(String role, String label) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 4),
        child: Row(children: [
          SizedBox(width: 72, child: Text(label)),
          Expanded(child: TextField(controller: ctl[role]![0], decoration: const InputDecoration(hintText: 'http://192.168.x.x'))),
          const SizedBox(width: 6),
          SizedBox(width: 90, child: TextField(controller: ctl[role]![1], obscureText: true, decoration: const InputDecoration(hintText: 'key'))),
          const SizedBox(width: 6),
          OutlinedButton(
            onPressed: () => guarded(context, () async {
              await appState.backend!.manualDevice(role, ctl[role]![0].text, ctl[role]![1].text.isEmpty ? null : ctl[role]![1].text);
              if (mounted) toast(context, 'Đã lưu');
            }),
            child: const Text('Lưu'),
          ),
        ]),
      );

  Future<void> _saveCalib() => guarded(context, () async {
        await appState.backend!.saveCalib(calib!);
        if (mounted) toast(context, 'Đã lưu vào thiết bị');
      });

  Widget _btns(List<Widget> children) => Wrap(spacing: 8, runSpacing: 8, children: children);

  @override
  Widget build(BuildContext context) => ListenableBuilder(
        listenable: appState,
        builder: (context, _) {
          final a = appState.actuator;
          final p = paletteOf(context);
          final full = a?['full'] as List?;
          final step = (a?['step'] != null && a!['step'] != 'idle') ? ' (${stepLabel('${a['step']}')})' : '';
          final line = a?['online'] == true
              ? '${a!['state']}$step · ngăn ${a['position']} · CB1 ${a['dist_full_cm'] ?? '--'} cm · CB2 ${a['dist_lid_cm'] ?? '--'} cm'
                  '${full != null ? ' · đầy: ${[for (var i = 0; i < full.length; i++) '${i + 1}${full[i] == true ? '✕' : '✓'}'].join(' ')}' : ''}'
              : 'Actuator offline';
          final warn = a?['tray_lost'] == true
              ? 'Mất vị trí mâm: nhích mâm về ngăn 1 rồi bấm "Đặt làm Home".'
              : (a?['needs_home'] == true ? 'Mâm có thể đã lệch: kiểm tra ngăn 1, nếu lệch thì jog và "Đặt làm Home".' : null);
          return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            const SectionTitle('Thiết bị'),
            Panel(children: [
              _deviceRow('cam', 'ESP32-CAM'),
              _deviceRow('actuator', 'Actuator'),
              Text(line, style: TextStyle(color: p.muted, fontFamily: 'monospace', fontSize: 13)),
              const SizedBox(height: 8),
              FilledButton(
                onPressed: () => guarded(context, () async {
                  await appState.backend!.rescan();
                  if (mounted) toast(context, 'Đã quét lại mạng');
                }),
                child: const Text('Kết nối lại'),
              ),
              ExpansionTile(
                tilePadding: EdgeInsets.zero, dense: true, title: Text('Nhập địa chỉ tay', style: TextStyle(fontSize: 13, color: p.muted)),
                children: [_manual('cam', 'CAM'), _manual('actuator', 'Actuator')],
              ),
            ]),
            if (warn != null)
              Container(
                width: double.infinity,
                margin: const EdgeInsets.only(bottom: 8),
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(border: Border.all(color: p.bad), borderRadius: BorderRadius.circular(8)),
                child: Text(warn, style: const TextStyle(fontSize: 13)),
              ),
            const SectionTitle('Thử nhanh'),
            Panel(title: 'Quay mâm tới', children: [
              _btns([for (final n in [1, 2, 3, 4]) OutlinedButton(onPressed: () => _act({'action': 'rotate', 'bin': n}, 'Quay tới ngăn $n'), child: Text('Ngăn $n'))]),
            ]),
            Panel(title: 'Nắp và hất', children: [
              _btns([
                OutlinedButton(onPressed: () => _act({'action': 'lid_open'}), child: const Text('Mở nắp')),
                OutlinedButton(onPressed: () => _act({'action': 'lid_close'}), child: const Text('Đóng nắp')),
                OutlinedButton(onPressed: () => _act({'action': 'dump'}), child: const Text('Hất rác')),
              ]),
            ]),
            Panel(title: 'Căn chỉnh mâm (ms)', children: [
              _btns([
                for (final j in [(-1, 30), (-1, 10), (1, 10), (1, 30)])
                  OutlinedButton(onPressed: () => _act({'action': 'jog', 'dir': j.$1, 'ms': j.$2}), child: Text('${j.$1 > 0 ? '+' : '−'}${j.$2}')),
                OutlinedButton(onPressed: () => _act({'action': 'home_set'}, 'Đã đặt vị trí này là Home (ngăn 1)'), child: const Text('Đặt làm Home')),
              ]),
            ]),
            const SectionTitle('Hiệu chuẩn'),
            if (calibLoaded && calib == null) Text('Không đọc được hiệu chuẩn (actuator offline?)', style: TextStyle(color: p.muted)),
            if (calib != null) ...[
              for (final g in _groups)
                Panel(children: [
                  ExpansionTile(
                    tilePadding: EdgeInsets.zero, dense: true, title: Text(g.$1),
                    childrenPadding: EdgeInsets.zero,
                    children: [
                  for (final f in g.$2.where((f) => calib!.containsKey(f.$1)))
                    labelled(f.$2, Stepper(value: calib![f.$1] as num, step: f.$3, onChanged: (v) => calib![f.$1] = v)),
                  if (g.$1 == 'Mâm quay') ...[
                    labelled('Ngăn 4 quay theo', SegmentedButton<int>(
                      showSelectedIcon: false,
                      segments: const [ButtonSegment(value: 1, label: Text('Thuận')), ButtonSegment(value: -1, label: Text('Nghịch'))],
                      selected: {((calib!['bin4_dir'] ?? 1) as num).toInt() >= 0 ? 1 : -1},
                      onSelectionChanged: (v) => setState(() => calib!['bin4_dir'] = v.first),
                    )),
                    labelled('Trim ngăn (ms)', Row(mainAxisSize: MainAxisSize.min, children: [
                      for (var i = 0; i < 4; i++)
                        SizedBox(width: 58, child: TextFormField(
                          initialValue: '${(calib!['trim_ms'] as List? ?? [0, 0, 0, 0])[i]}',
                          keyboardType: TextInputType.number, textAlign: TextAlign.center,
                          onChanged: (v) { (calib!['trim_ms'] as List)[i] = int.tryParse(v) ?? 0; })),
                    ])),
                  ],
                  if (g.$1 == 'Nắp')
                    OutlinedButton(
                      onPressed: () async {
                        final t = calib!['lid_rest'];
                        calib!['lid_rest'] = calib!['lid_open'];
                        calib!['lid_open'] = t;
                        await _saveCalib();
                        if (mounted) setState(() {});
                      },
                      child: const Text('Đảo chiều nắp'),
                    ),
                    ]),
                ]),
              SizedBox(width: double.infinity, height: 48, child: FilledButton(onPressed: _saveCalib, child: const Text('Lưu hiệu chuẩn'))),
            ],
          ]);
        },
      );
}
