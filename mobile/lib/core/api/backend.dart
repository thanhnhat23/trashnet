import 'dart:async';

/// One event from the backend: type = hello|device|sort|result|actuator.
class TnEvent {
  final String type;
  final Map<String, dynamic> data;
  const TnEvent(this.type, this.data);
}

/// Everything the screens need. ServerBackend talks to the PC server, StandaloneBackend talks to the ESP32s directly.
abstract class Backend {
  Stream<TnEvent> events();
  Future<List<Map<String, dynamic>>> labels();
  Future<Map<String, dynamic>> sortCapture();
  Future<void> setAuto(bool on);
  Future<Map<String, dynamic>> setReference();
  Future<void> flash(bool on);
  Future<void> rescan();
  Future<Map<String, dynamic>> history(String label);
  Future<Map<String, dynamic>> stats();
  Future<void> deleteHistory([String? id]);
  Future<Map<String, dynamic>> settings();
  Future<void> patchSettings(Map<String, dynamic> values);
  Future<void> control(Map<String, dynamic> body);
  Future<Map<String, dynamic>?> calib();
  Future<void> saveCalib(Map<String, dynamic> c);
  Future<void> manualDevice(String role, String url, String? key);
  String get streamUrl;
  String? thumbUrl(String? path);
  Map<String, String> get headers;
  void dispose();
}

class ApiError implements Exception {
  final String message;
  const ApiError(this.message);
  @override
  String toString() => message;
}
