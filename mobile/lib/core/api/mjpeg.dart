import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/widgets.dart';
import 'package:http/http.dart' as http;

/// Splits an MJPEG byte stream into JPEG frames (SOI FFD8 ... EOI FFD9).
Stream<Uint8List> mjpegFrames(String url, Map<String, String> headers) async* {
  final client = http.Client();
  try {
    final res = await client.send(http.Request('GET', Uri.parse(url))..headers.addAll(headers));
    var acc = <int>[];
    await for (final part in res.stream) {
      acc.addAll(part);
      while (true) {
        final s = _find(acc, 0xFF, 0xD8, 0);
        if (s < 0) { acc = []; break; }
        final e = _find(acc, 0xFF, 0xD9, s + 2);
        if (e < 0) { if (s > 0) acc = acc.sublist(s); break; }
        yield Uint8List.fromList(acc.sublist(s, e + 2));
        acc = acc.sublist(e + 2);
      }
      if (acc.length > 2 * 1024 * 1024) acc = [];
    }
  } finally {
    client.close();
  }
}

int _find(List<int> d, int a, int b, int from) {
  for (var i = from; i < d.length - 1; i++) {
    if (d[i] == a && d[i + 1] == b) return i;
  }
  return -1;
}

/// Live camera view; reconnects when the stream drops.
class MjpegView extends StatefulWidget {
  const MjpegView({super.key, required this.url, this.headers = const {}});
  final String url;
  final Map<String, String> headers;
  @override
  State<MjpegView> createState() => _MjpegViewState();
}

class _MjpegViewState extends State<MjpegView> {
  Uint8List? _frame;
  StreamSubscription? _sub;
  bool _alive = true;

  @override
  void initState() {
    super.initState();
    _run();
  }

  Future<void> _run() async {
    while (_alive) {
      final done = Completer<void>();
      _sub = mjpegFrames(widget.url, widget.headers).listen(
        (f) { if (mounted) setState(() => _frame = f); },
        onDone: done.complete, onError: (_) => done.complete(), cancelOnError: true);
      await done.future;
      await Future.delayed(const Duration(seconds: 2));
    }
  }

  @override
  void dispose() {
    _alive = false;
    _sub?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => _frame == null
      ? const SizedBox.expand()
      : Image.memory(_frame!, fit: BoxFit.contain, gaplessPlayback: true, width: double.infinity, height: double.infinity);
}
