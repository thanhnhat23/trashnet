import 'package:flutter/material.dart';

import 'tokens.dart';

/// Minimalist text UI, identical tokens to the web (shared/design-tokens.json).
ThemeData tnTheme(Brightness b) {
  final p = b == Brightness.dark ? TnPalette.dark : TnPalette.light;
  final base = ThemeData(brightness: b, useMaterial3: true);
  return base.copyWith(
    scaffoldBackgroundColor: p.bg,
    colorScheme: ColorScheme.fromSeed(seedColor: p.text, brightness: b).copyWith(
        surface: p.bg, primary: p.text, onPrimary: p.bg, outline: p.line, onSurface: p.text),
    dividerColor: p.line,
    textTheme: base.textTheme.apply(bodyColor: p.text, displayColor: p.text).copyWith(
          bodyMedium: TextStyle(fontSize: tnFsMd, color: p.text, height: 1.5),
          bodySmall: TextStyle(fontSize: tnFsSm, color: p.muted),
        ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        foregroundColor: p.text, side: BorderSide(color: p.line),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(tnRadius)),
        padding: EdgeInsets.symmetric(horizontal: tnSpace[2], vertical: tnSpace[0]),
        minimumSize: const Size(0, 36)),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        backgroundColor: p.text, foregroundColor: p.bg,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(tnRadius)),
        minimumSize: const Size(0, 36)),
    ),
    inputDecorationTheme: InputDecorationTheme(
      isDense: true,
      border: OutlineInputBorder(borderRadius: BorderRadius.circular(tnRadius), borderSide: BorderSide(color: p.line)),
      enabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(tnRadius), borderSide: BorderSide(color: p.line)),
    ),
    appBarTheme: AppBarTheme(backgroundColor: p.bg, elevation: 0, scrolledUnderElevation: 0, foregroundColor: p.text),
    navigationBarTheme: NavigationBarThemeData(
      backgroundColor: p.bg, indicatorColor: Colors.transparent, height: 52,
      labelTextStyle: WidgetStateProperty.resolveWith((s) => TextStyle(
          fontSize: tnFsSm, color: s.contains(WidgetState.selected) ? p.text : p.muted,
          decoration: s.contains(WidgetState.selected) ? TextDecoration.underline : null)),
    ),
  );
}

TnPalette paletteOf(BuildContext c) => Theme.of(c).brightness == Brightness.dark ? TnPalette.dark : TnPalette.light;
