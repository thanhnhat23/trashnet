import 'package:flutter/material.dart';

import '../../core/labels.dart';
import '../../core/state/app_state.dart';
import '../../core/theme/app_theme.dart';
import '../../core/widgets.dart';

class StatsPage extends StatefulWidget {
  const StatsPage({super.key});
  @override
  State<StatsPage> createState() => _StatsPageState();
}

class _StatsPageState extends State<StatsPage> {
  Map<String, dynamic>? s;

  @override
  void initState() {
    super.initState();
    appState.backend!.stats().then((v) { if (mounted) setState(() => s = v); }).catchError((e) { if (mounted) toast(context, '$e'); });
  }

  @override
  Widget build(BuildContext context) {
    if (s == null) return const SizedBox.shrink();
    final by = Map<String, dynamic>.from(s!['by_label'] ?? {});
    final byDay = (s!['by_day'] as List? ?? []);
    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [
        Expanded(child: StatTile('Đã phân loại', '${s!['total']}')),
        const SizedBox(width: 8),
        Expanded(child: StatTile('Tin cậy TB', '${(((s!['avg_confidence'] ?? 0) as num) * 100).round()}%')),
        const SizedBox(width: 8),
        Expanded(child: StatTile('Không chắc', '${s!['uncertain']}')),
      ]),
      const SectionTitle('Theo loại rác'),
      Donut([for (final b in tnBins) (label: b.nameVi, value: ((by[b.id] ?? 0) as num).toInt(), color: b.color)]),
      const SectionTitle('14 ngày gần nhất'),
      if (byDay.isEmpty) Text('Chưa có dữ liệu.', style: TextStyle(color: paletteOf(context).muted)),
      if (byDay.isNotEmpty) Columns([for (final d in byDay) ('${d['day']}'.substring(5), (d['n'] as num).toInt())]),
    ]);
  }
}
