import 'package:flutter/material.dart';

/// ForkMesh mobile palette for the professional light UI refresh.
class FmColors {
  static const canvas = Color(0xFFF6F7F9);
  static const surface = Color(0xFFFFFFFF);
  static const rail = Color(0xFFFFFFFF);
  static const border = Color(0xFFE1E4EA);
  static const borderMuted = Color(0xFFF0F2F5);
  static const text = Color(0xFF111318);
  static const textMuted = Color(0xFF69717F);
  static const accent = Color(0xFF1769E0);
  static const success = Color(0xFF148A45);
  static const successBtn = Color(0xFF111318);
  static const successBtnHover = Color(0xFF2A2E36);
  static const danger = Color(0xFFC93532);
  static const warning = Color(0xFFB7791F);
  static const offline = Color(0xFFA0A7B2);
  static const accentEdge = Color(0xFF8250DF);

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
  const scheme = ColorScheme.light(
    primary: FmColors.text,
    secondary: FmColors.accent,
    surface: FmColors.surface,
    error: FmColors.danger,
    onPrimary: Colors.white,
    onSurface: FmColors.text,
  );
  return ThemeData(
    useMaterial3: true,
    brightness: Brightness.light,
    colorScheme: scheme,
    scaffoldBackgroundColor: FmColors.canvas,
    canvasColor: FmColors.canvas,
    dividerColor: FmColors.border,
    fontFamily: 'Inter',
    appBarTheme: const AppBarTheme(
      backgroundColor: FmColors.surface,
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
      fillColor: FmColors.surface,
      hintStyle: const TextStyle(color: FmColors.textMuted),
      labelStyle: const TextStyle(color: FmColors.textMuted),
      contentPadding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(16),
        borderSide: const BorderSide(color: FmColors.border),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(16),
        borderSide: const BorderSide(color: FmColors.text, width: 1.2),
      ),
    ),
    elevatedButtonTheme: ElevatedButtonThemeData(
      style: ElevatedButton.styleFrom(
        backgroundColor: FmColors.successBtn,
        foregroundColor: Colors.white,
        elevation: 0,
        shape: const StadiumBorder(),
      ),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        backgroundColor: FmColors.text,
        foregroundColor: Colors.white,
        elevation: 0,
        shape: const StadiumBorder(),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        foregroundColor: FmColors.text,
        side: const BorderSide(color: FmColors.border),
        shape: const StadiumBorder(),
      ),
    ),
    listTileTheme: const ListTileThemeData(iconColor: FmColors.textMuted),
    dividerTheme: const DividerThemeData(
      color: FmColors.border,
      space: 1,
      thickness: 1,
    ),
  );
}
