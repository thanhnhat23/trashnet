import 'package:flutter/material.dart' hide Stepper;

import '../../core/state/app_state.dart';
import '../../core/theme/app_theme.dart';
import '../../core/widgets.dart';

/// (key, kind, name, tooltip, step, min, max, unit, advanced)
typedef SetField = (String, String, String, String, num, num, num, String, bool);

class SettingsPage extends StatefulWidget {
  const SettingsPage({super.key});
  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> {
  Map<String, dynamic>? cfg;
  final urlCtl = TextEditingController(text: appState.serverUrl);
  final keyCtl = TextEditingController(text: appState.apiKey);
  Mode mode = appState.mode;

  // (key, kind, name, tooltip, step, min, max, unit, advanced). kind: pct = 0..1 shown as %, num, bool.
  static const List<(String, String, List<SetField>)> sections = [
    ('Nhận diện', 'Model nhìn rác thế nào và khi nào thì coi là chắc chắn.', [
      ('det_tau', 'pct', 'Độ chắc chắn tối thiểu', 'Model phải chắc tới mức này thì mới bỏ rác vào đúng ngăn; thấp hơn thì đưa vào ngăn Rác khác. Tăng: ít bỏ nhầm ngăn hơn nhưng nhiều rác vào Rác khác hơn. Nên đặt 40–55%.', 0.01, 0.0, 1.0, '', false),
      ('det_conf', 'pct', 'Ngưỡng nhìn thấy vật', 'Từ mức này model mới coi là trên khay có vật. Thấp: nhạy hơn nhưng dễ báo nhầm bóng hoặc vết bẩn. Nên đặt 25–35%.', 0.01, 0.0, 1.0, '', false),
      ('presence_min_diff', 'num', 'Độ nhạy khay trắng', 'So với ảnh khay trống: lệch bao nhiêu mức xám thì coi là có vật (giấy trắng trên khay trắng). Nhỏ: nhạy hơn nhưng dễ nhầm khi ánh sáng đổi. Nên đặt 8–20.', 1, 1, 60, 'mức xám', false),
      ('vote_frames', 'num', 'Số ảnh mỗi lần', 'Chụp ngần này ảnh rồi lấy kết quả theo đa số. Nhiều ảnh: chắc hơn nhưng chậm hơn. Nên đặt 3.', 1, 1, 7, 'ảnh', false),
      ('blur_min', 'num', 'Độ nét tối thiểu', 'Ảnh kém nét hơn mức này thì chụp lại (tối đa 3 lần). Nếu máy chụp lại liên tục thì giảm xuống. Nên đặt 30–80.', 5, 0, 500, '', true),
      ('min_area_frac', 'pct', 'Kích thước vật tối thiểu', 'Tính theo phần trăm khung hình; vật nhỏ hơn bị bỏ qua. Nên đặt 1–3%.', 0.005, 0.0, 0.2, '', true),
    ]),
    ('Tự động', 'Khi bật Tự động phân loại ở trang Live.', [
      ('auto_interval_sec', 'num', 'Kiểm tra khay mỗi', 'Máy nhìn khay bao lâu một lần; cần thấy vật 2 lần liên tiếp mới phân loại. Nên đặt 1–3.', 0.5, 0.5, 30, 'giây', false),
      ('cooldown_sec', 'num', 'Nghỉ sau mỗi lần', 'Chờ sau mỗi lần hất để rác rơi hẳn rồi mới nhìn tiếp. Nên đặt 1–3.', 0.5, 0, 30, 'giây', false),
    ]),
    ('Camera', '', [
      ('capture_from_stream', 'bool', 'Chụp từ luồng stream', 'Bật: lấy ảnh từ stream đang chạy nên nhanh hơn và camera không bị đứng hình khi phân loại.', 0, 0, 0, '', false),
    ]),
  ];

  Widget _tip(String t) => Tooltip(
        message: t, triggerMode: TooltipTriggerMode.tap, showDuration: const Duration(seconds: 8),
        child: Padding(padding: const EdgeInsets.only(left: 6), child: Icon(Icons.info_outline, size: 16, color: paletteOf(context).muted)),
      );

  Widget _field(SetField f) {
    final (k, kind, name, tip, step, mn, mx, unit, _) = f;
    final model = Map<String, dynamic>.from((cfg!['_meta'] as Map?)?['model'] ?? {});
    Widget ctl;
    if (kind == 'bool') {
      ctl = Switch(value: cfg![k] == true, onChanged: (v) => setState(() => cfg![k] = v));
    } else if (kind == 'pct') {
      final v = ((cfg![k] ?? model[k] ?? 0) as num).toDouble().clamp(mn.toDouble(), mx.toDouble());
      ctl = Row(mainAxisSize: MainAxisSize.min, children: [
        SizedBox(width: 130, child: Slider(value: v, min: mn.toDouble(), max: mx.toDouble(),
            divisions: ((mx - mn) / step).round(), onChanged: (x) => setState(() => cfg![k] = double.parse(x.toStringAsFixed(3))))),
        SizedBox(width: 44, child: Text('${(v * 1000).round() / 10}%', textAlign: TextAlign.right, style: const TextStyle(fontFamily: 'monospace'))),
        if (model.containsKey(k))
          TextButton(onPressed: () => setState(() => cfg![k] = null), child: Text(cfg![k] == null ? 'Theo model' : 'Mặc định')),
      ]);
    } else {
      ctl = Row(mainAxisSize: MainAxisSize.min, children: [
        Stepper(key: ValueKey('$k${cfg![k]}'), value: (cfg![k] ?? 0) as num, step: step, onChanged: (x) => cfg![k] = x.clamp(mn, mx)),
        if (unit.isNotEmpty) Text(unit, style: TextStyle(fontSize: 12, color: paletteOf(context).muted)),
      ]);
    }
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Wrap(alignment: WrapAlignment.spaceBetween, crossAxisAlignment: WrapCrossAlignment.center, runSpacing: 4, children: [
        Row(mainAxisSize: MainAxisSize.min, children: [Text(name, style: const TextStyle(fontWeight: FontWeight.w500)), _tip(tip)]),
        ctl,
      ]),
    );
  }

  Widget _section((String, String, List<SetField>) s) {
    final main = s.$3.where((f) => !f.$9).toList(), adv = s.$3.where((f) => f.$9).toList();
    return Panel(title: s.$1, children: [
      if (s.$2.isNotEmpty) Text(s.$2, style: TextStyle(fontSize: 13, color: paletteOf(context).muted)),
      for (final f in main) _field(f),
      if (adv.isNotEmpty)
        ExpansionTile(tilePadding: EdgeInsets.zero, dense: true, title: const Text('Nâng cao'), childrenPadding: EdgeInsets.zero,
            children: [for (final f in adv) _field(f)]),
    ]);
  }

  @override
  void initState() {
    super.initState();
    appState.backend!.settings().then((v) { if (mounted) setState(() => cfg = v); }).catchError((e) { if (mounted) toast(context, '$e'); });
  }

  @override
  Widget build(BuildContext context) => Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        const SectionTitle('Kết nối'),
        labelled('Chế độ', DropdownButton<Mode>(
          value: mode,
          items: const [
            DropdownMenuItem(value: Mode.standalone, child: Text('Độc lập (điện thoại ↔ ESP32)')),
            DropdownMenuItem(value: Mode.server, child: Text('Qua server PC')),
          ],
          onChanged: (v) => setState(() => mode = v ?? mode),
        )),
        if (mode == Mode.server) ...[
          const SizedBox(height: 8),
          TextField(controller: urlCtl, decoration: const InputDecoration(hintText: 'http://192.168.1.10:8000')),
          const SizedBox(height: 8),
          TextField(controller: keyCtl, obscureText: true, decoration: const InputDecoration(hintText: 'API key của server')),
        ],
        const SizedBox(height: 8),
        FilledButton(
          onPressed: () => guarded(context, () async {
            await appState.saveConnection(newMode: mode, url: urlCtl.text, key: keyCtl.text);
            if (mounted) toast(context, 'Đã kết nối lại');
          }),
          child: const Text('Lưu kết nối'),
        ),
        if (cfg != null) ...[
          const SectionTitle('Cài đặt'),
          for (final sec in sections) _section(sec),
          Panel(children: [
            Wrap(alignment: WrapAlignment.spaceBetween, crossAxisAlignment: WrapCrossAlignment.center, children: [
              Row(mainAxisSize: MainAxisSize.min, children: [const Text('Độ phân giải chụp', style: TextStyle(fontWeight: FontWeight.w500)),
                _tip('SVGA là mức cân bằng. XGA nét hơn nhưng chụp chậm hơn và stream giật hơn.')]),
              DropdownButton<String>(
                value: (cfg!['capture']?['res'] ?? 'svga') as String,
                items: const [DropdownMenuItem(value: 'vga', child: Text('VGA 640×480')), DropdownMenuItem(value: 'svga', child: Text('SVGA 800×600')),
                  DropdownMenuItem(value: 'xga', child: Text('XGA 1024×768'))],
                onChanged: (v) => setState(() { cfg!['capture'] = {...?cfg!['capture'] as Map?, 'res': v}; }),
              ),
            ]),
          ]),
          const SizedBox(height: 8),
          FilledButton(
            onPressed: () => guarded(context, () async {
              final v = Map<String, dynamic>.from(cfg!)..remove('api_key')..remove('device_keys')..remove('_meta');
              await appState.backend!.patchSettings(v);
              if (mounted) toast(context, 'Đã lưu');
            }),
            child: const Text('Lưu thay đổi'),
          ),
        ],
        const SectionTitle('Giao diện'),
        labelled('Chủ đề', DropdownButton<String>(
          value: appState.theme,
          items: const [
            DropdownMenuItem(value: '', child: Text('Theo hệ thống')),
            DropdownMenuItem(value: 'light', child: Text('Sáng')),
            DropdownMenuItem(value: 'dark', child: Text('Tối')),
          ],
          onChanged: (v) => appState.setTheme(v ?? ''),
        )),
      ]);
}
