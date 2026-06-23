import 'package:flutter/material.dart';

/// ForkMesh palette, ported from the Qt client's Theme.h (GitHub Primer).
///   canvas #0d1117  surface #161b22  header/rail #010409
///   border #30363d / muted #21262d
///   text #e6edf3 / muted #8b949e   accent #58a6ff
///   success #238636 / #2ea043      danger #da3633 / #f85149
class FmColors {
  // Dark
  static const canvas = Color(0xFF0D1117);
  static const surface = Color(0xFF161B22);
  static const rail = Color(0xFF010409);
  static const border = Color(0xFF30363D);
  static const borderMuted = Color(0xFF21262D);
  static const text = Color(0xFFE6EDF3);
  static const textMuted = Color(0xFF8B949E);
  static const accent = Color(0xFF58A6FF);
  static const success = Color(0xFF3FB950);
  static const successBtn = Color(0xFF238636);
  static const successBtnHover = Color(0xFF2EA043);
  static const danger = Color(0xFFF85149);
  static const warning = Color(0xFFD29922);
  static const offline = Color(0xFF8B949E);
  static const accentEdge = Color(0xFFFD8C73);

  // Sender name palette (hashed per user), mirrors kSenderPalette.
  static const senders = <Color>[
    Color(0xFFF85149),
    Color(0xFFE3B341),
    Color(0xFF3FB950),
    Color(0xFF58A6FF),
    Color(0xFFBC8CFF),
    Color(0xFFDB61A2),
    Color(0xFF39C5CF),
    Color(0xFFDB6D28),
  ];

  static Color senderColor(String name) {
    var h = 0;
    for (final c in name.codeUnits) {
      h = (h * 31 + c) & 0x7fffffff;
    }
    return senders[h % senders.length];
  }
}

ThemeData buildForkMeshTheme() {
  const scheme = ColorScheme.dark(
    primary: FmColors.accent,
    secondary: FmColors.successBtn,
    surface: FmColors.surface,
    error: FmColors.danger,
    onPrimary: Colors.white,
    onSurface: FmColors.text,
  );
  return ThemeData(
    useMaterial3: true,
    brightness: Brightness.dark,
    colorScheme: scheme,
    scaffoldBackgroundColor: FmColors.canvas,
    canvasColor: FmColors.canvas,
    dividerColor: FmColors.border,
    fontFamily: 'Inter',
    appBarTheme: const AppBarTheme(
      backgroundColor: FmColors.canvas,
      surfaceTintColor: Colors.transparent,
      foregroundColor: FmColors.text,
      elevation: 0,
    ),
    cardTheme: const CardThemeData(
      color: FmColors.surface,
      elevation: 0,
      margin: EdgeInsets.zero,
      shape: RoundedRectangleBorder(
        side: BorderSide(color: FmColors.border),
        borderRadius: BorderRadius.all(Radius.circular(8)),
      ),
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: FmColors.canvas,
      hintStyle: const TextStyle(color: FmColors.textMuted),
      contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(6),
        borderSide: const BorderSide(color: FmColors.border),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(6),
        borderSide: const BorderSide(color: FmColors.accent),
      ),
    ),
    elevatedButtonTheme: ElevatedButtonThemeData(
      style: ElevatedButton.styleFrom(
        backgroundColor: FmColors.successBtn,
        foregroundColor: Colors.white,
        elevation: 0,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
      ),
    ),
    listTileTheme: const ListTileThemeData(iconColor: FmColors.textMuted),
    dividerTheme: const DividerThemeData(color: FmColors.border, space: 1, thickness: 1),
  );
}
