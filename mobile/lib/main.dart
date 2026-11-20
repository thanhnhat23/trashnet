import 'package:flutter/material.dart';

import 'core/state/app_state.dart';
import 'core/theme/app_theme.dart';
import 'core/widgets.dart';
import 'features/devices/devices_page.dart';
import 'features/history/history_page.dart';
import 'features/live/live_page.dart';
import 'features/settings/settings_page.dart';
import 'features/stats/stats_page.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await appState.init();
  runApp(const TrashNetApp());
}

class TrashNetApp extends StatelessWidget {
  const TrashNetApp({super.key});
  @override
  Widget build(BuildContext context) => ListenableBuilder(
        listenable: appState,
        builder: (_, __) => MaterialApp(
          title: 'TrashNet',
          debugShowCheckedModeBanner: false,
          theme: tnTheme(Brightness.light),
          darkTheme: tnTheme(Brightness.dark),
          themeMode: {'light': ThemeMode.light, 'dark': ThemeMode.dark}[appState.theme] ?? ThemeMode.system,
          home: const Shell(),
        ),
      );
}

class Shell extends StatefulWidget {
  const Shell({super.key});
  @override
  State<Shell> createState() => _ShellState();
}

class _ShellState extends State<Shell> {
  int tab = 0;
  static const names = ['Live', 'Lịch sử', 'Thống kê', 'Thiết bị', 'Cài đặt'];

  Widget _page() => switch (tab) {
        0 => const LivePage(),
        1 => const HistoryPage(),
        2 => const StatsPage(),
        3 => const DevicesPage(),
        _ => const SettingsPage(),
      };

  @override
  Widget build(BuildContext context) => ListenableBuilder(
        listenable: appState,
        builder: (context, _) {
          final d = appState.devices;
          return Scaffold(
            appBar: AppBar(
              titleSpacing: 16,
              title: const Text('TrashNet', style: TextStyle(fontSize: 15, fontWeight: FontWeight.bold, letterSpacing: 1)),
              actions: [
                Padding(
                  padding: const EdgeInsets.only(right: 16),
                  child: Row(children: [
                    StatusDot(appState.connected ? 'ok' : 'bad', appState.mode == Mode.server ? 'server' : 'app'),
                    const SizedBox(width: 8),
                    StatusDot(d['cam']?['online'] == true ? 'ok' : 'bad', 'cam'),
                    const SizedBox(width: 8),
                    StatusDot(d['actuator']?['online'] == true ? 'ok' : 'bad', 'act'),
                  ]),
                ),
              ],
            ),
            body: SafeArea(child: SingleChildScrollView(padding: EdgeInsets.all(tab == 0 ? 8 : 16), child: Center(
              child: ConstrainedBox(constraints: BoxConstraints(maxWidth: tab == 0 ? 1400 : 1040), child: _page())))),
            bottomNavigationBar: NavigationBar(
              selectedIndex: tab,
              onDestinationSelected: (i) => setState(() => tab = i),
              labelBehavior: NavigationDestinationLabelBehavior.alwaysShow,
              destinations: [for (final n in names) NavigationDestination(icon: const SizedBox.shrink(), label: n)],
            ),
          );
        },
      );
}
