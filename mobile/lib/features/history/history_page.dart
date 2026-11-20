import 'package:flutter/material.dart';

import '../../core/labels.dart';
import '../../core/state/app_state.dart';
import '../../core/theme/app_theme.dart';
import '../../core/widgets.dart';

class HistoryPage extends StatefulWidget {
  const HistoryPage({super.key});
  @override
  State<HistoryPage> createState() => _HistoryPageState();
}

class _HistoryPageState extends State<HistoryPage> {
  String filter = '';
  List items = [];
  bool loaded = false;

  @override
  void initState() {
    super.initState();
    appState.addListener(_onResult);
    _load();
  }

  Object? _last;
  void _onResult() {
    final id = appState.result?['id'];
    if (id != null && id != _last) { _last = id; _load(); }
  }

  @override
  void dispose() {
    appState.removeListener(_onResult);
    super.dispose();
  }

  Future<void> _load() async {
    try {
      final d = await appState.backend!.history(filter);
      if (mounted) setState(() { items = d['items'] as List; loaded = true; });
    } catch (e) {
      if (mounted) toast(context, '$e');
    }
  }

  @override
  Widget build(BuildContext context) {
    final p = paletteOf(context);
    final b = appState.backend!;
    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      const SectionTitle('Lịch sử'),
      Row(children: [
        DropdownButton<String>(
          value: filter,
          items: [const DropdownMenuItem(value: '', child: Text('Tất cả')), for (final t in tnBins) DropdownMenuItem(value: t.id, child: Text(t.nameVi))],
          onChanged: (v) { filter = v ?? ''; _load(); },
        ),
        const SizedBox(width: 8),
        OutlinedButton(
          onPressed: () async {
            final ok = await showDialog<bool>(
              context: context,
              builder: (c) => AlertDialog(title: const Text('Xóa toàn bộ lịch sử?'), actions: [
                TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('Hủy')),
                TextButton(onPressed: () => Navigator.pop(c, true), child: const Text('Xóa')),
              ]),
            );
            if (ok == true && context.mounted) await guarded(context, () async { await b.deleteHistory(); await _load(); });
          },
          child: const Text('Xóa tất cả'),
        ),
      ]),
      const SizedBox(height: 12),
      if (loaded && items.isEmpty) Text('Chưa có bản ghi.', style: TextStyle(color: p.muted)),
      GridView.builder(
        shrinkWrap: true, physics: const NeverScrollableScrollPhysics(), itemCount: items.length,
        gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(maxCrossAxisExtent: 180, mainAxisSpacing: 12, crossAxisSpacing: 12, childAspectRatio: 0.78),
        itemBuilder: (_, i) {
          final r = items[i] as Map<String, dynamic>;
          final bin = tnBinById('${r['label']}');
          final ts = DateTime.fromMillisecondsSinceEpoch(((r['ts'] as num) * 1000).round());
          final thumb = b.thumbUrl(r['thumb'] as String?);
          return Container(
            decoration: BoxDecoration(border: Border.all(color: p.line), borderRadius: BorderRadius.circular(8)),
            clipBehavior: Clip.antiAlias,
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              AspectRatio(aspectRatio: 4 / 3, child: thumb == null ? Container(color: p.surface) : Image.network(thumb, headers: b.headers, fit: BoxFit.cover)),
              Padding(
                padding: const EdgeInsets.all(8),
                child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  Text('${bin.nameVi} · ${((r['confidence'] as num) * 100).round()}%', style: TextStyle(color: bin.color, fontWeight: FontWeight.w600, fontSize: 13)),
                  Text('${ts.day}/${ts.month} ${ts.hour.toString().padLeft(2, '0')}:${ts.minute.toString().padLeft(2, '0')}${r['uncertain'] == true ? ' · không chắc' : ''}',
                      style: TextStyle(color: p.muted, fontSize: 11)),
                  const SizedBox(height: 4),
                  SizedBox(height: 28, child: OutlinedButton(onPressed: () => guarded(context, () async { await b.deleteHistory('${r['id']}'); await _load(); }), child: const Text('Xóa', style: TextStyle(fontSize: 12)))),
                ]),
              ),
            ]),
          );
        },
      ),
    ]);
  }
}
