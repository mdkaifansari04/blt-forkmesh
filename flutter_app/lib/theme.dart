import 'package:flutter/material.dart';

/// ForkMesh spacing scale for the Lunel-inspired mobile refresh.
class FmSpace {
  static const double x0 = 0;
  static const double x1 = 4;
  static const double x2 = 8;
  static const double x3 = 12;
  static const double x4 = 16;
  static const double x5 = 24;
  static const double x6 = 32;
  static const double x7 = 48;
  static const double x8 = 64;
}

/// ForkMesh radius scale.
class FmRadius {
  static const double sm = 4;
  static const double md = 8;
  static const double lg = 12;
  static const double full = 999;
}

/// ForkMesh motion durations.
class FmMotion {
  static const fast = Duration(milliseconds: 100);
  static const normal = Duration(milliseconds: 200);
}

/// ForkMesh mobile palette for the professional light UI refresh.
class FmColors {
  static const bgBase = Color(0xFFF7F7F7);
  static const bgRaised = Color(0xFFFFFFFF);
  static const bgOverlay = Color(0xFFF0F0F0);
  static const bgElevated = Color(0xFFE8E8E8);

  static const darkBgBase = Color(0xFF0B0B0C);
  static const darkBgRaised = Color(0xFF141414);
  static const darkBgOverlay = Color(0xFF202022);
  static const darkBgElevated = Color(0xFF26262A);

  static const textPrimary = Color(0xFF0A0A0A);
  static const textSecondary = Color(0x990A0A0A);
  static const textTertiary = Color(0x660A0A0A);
  static const textDisabled = Color(0x400A0A0A);

  static const darkTextPrimary = Color(0xFFF4F4F5);
  static const darkTextSecondary = Color(0xB3F4F4F5);
  static const darkTextTertiary = Color(0x80F4F4F5);
  static const darkTextDisabled = Color(0x4DF4F4F5);

  static const accent = Color(0xFF6161F2);
  static const accentSubtle = Color(0x1A6161F2);
  static const accentOnDark = Color(0xFF6C63FF);
  static const accentSubtleOnDark = Color(0x332F2A74);
  static const success = Color(0xFF148A45);
  static const successOnDark = Color(0xFF46E07F);
  static const successBtn = textPrimary;
  static const successBtnHover = Color(0xFF2A2E36);
  static const danger = Color(0xFFC93532);
  static const dangerOnDark = Color(0xFFFF6B6B);
  static const warning = Color(0xFFB7791F);
  static const warningOnDark = Color(0xFFE5C84C);
  static const offline = Color(0xFFA0A7B2);
  static const accentEdge = Color(0xFF8250DF);
  static const darkBorder = Color(0xFF252529);
  static const editorGutter = Color(0xFF101012);
  static const editorCurrentLine = Color(0xFF202024);
  static const codeKeyword = Color(0xFFC792EA);
  static const codeType = Color(0xFFFF6E73);
  static const codeString = Color(0xFFC3E88D);
  static const codeComment = Color(0xFF777A82);
  static const codeNumber = Color(0xFFF78C6C);
  static const codeFunction = Color(0xFF82AAFF);

  static const canvas = bgBase;
  static const surface = bgRaised;
  static const rail = bgRaised;
  static const border = Color(0xFFDADADA);
  static const borderMuted = bgOverlay;
  static const text = textPrimary;
  static const textMuted = textSecondary;

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

class FmTheme {
  static bool isDark(BuildContext context) =>
      Theme.of(context).brightness == Brightness.dark;

  static Color bgBase(BuildContext context) =>
      isDark(context) ? FmColors.darkBgBase : FmColors.bgBase;
  static Color bgRaised(BuildContext context) =>
      isDark(context) ? FmColors.darkBgRaised : FmColors.bgRaised;
  static Color bgOverlay(BuildContext context) =>
      isDark(context) ? FmColors.darkBgOverlay : FmColors.bgOverlay;
  static Color bgElevated(BuildContext context) =>
      isDark(context) ? FmColors.darkBgElevated : FmColors.bgElevated;
  static Color textPrimary(BuildContext context) =>
      isDark(context) ? FmColors.darkTextPrimary : FmColors.textPrimary;
  static Color textSecondary(BuildContext context) =>
      isDark(context) ? FmColors.darkTextSecondary : FmColors.textSecondary;
  static Color textTertiary(BuildContext context) =>
      isDark(context) ? FmColors.darkTextTertiary : FmColors.textTertiary;
  static Color textDisabled(BuildContext context) =>
      isDark(context) ? FmColors.darkTextDisabled : FmColors.textDisabled;
  static Color accent(BuildContext context) =>
      isDark(context) ? FmColors.accentOnDark : FmColors.accent;
  static Color accentSubtle(BuildContext context) =>
      isDark(context) ? FmColors.accentSubtleOnDark : FmColors.accentSubtle;
  static Color success(BuildContext context) =>
      isDark(context) ? FmColors.successOnDark : FmColors.success;
  static Color danger(BuildContext context) =>
      isDark(context) ? FmColors.dangerOnDark : FmColors.danger;
  static Color warning(BuildContext context) =>
      isDark(context) ? FmColors.warningOnDark : FmColors.warning;
  static Color border(BuildContext context) =>
      isDark(context) ? FmColors.darkBorder : FmColors.borderMuted;
  static Color activeUnderline(BuildContext context) =>
      isDark(context) ? Colors.white : FmColors.textPrimary;
}

ThemeData buildForkMeshLightTheme() {
  const scheme = ColorScheme.light(
    primary: FmColors.textPrimary,
    secondary: FmColors.accent,
    surface: FmColors.bgRaised,
    error: FmColors.danger,
    onPrimary: Colors.white,
    onSurface: FmColors.textPrimary,
  );
  return ThemeData(
    useMaterial3: true,
    brightness: Brightness.light,
    colorScheme: scheme,
    scaffoldBackgroundColor: FmColors.bgBase,
    canvasColor: FmColors.bgBase,
    dividerColor: FmColors.borderMuted,
    textTheme: _forkMeshTextTheme(Brightness.light),
    appBarTheme: const AppBarTheme(
      backgroundColor: FmColors.bgRaised,
      surfaceTintColor: Colors.transparent,
      foregroundColor: FmColors.textPrimary,
      elevation: 0,
    ),
    cardTheme: const CardThemeData(
      color: FmColors.bgRaised,
      elevation: 0,
      margin: EdgeInsets.zero,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.all(Radius.circular(FmRadius.md)),
      ),
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: FmColors.bgRaised,
      hintStyle: const TextStyle(color: FmColors.textTertiary),
      labelStyle: const TextStyle(color: FmColors.textSecondary),
      contentPadding: const EdgeInsets.symmetric(
        horizontal: FmSpace.x4,
        vertical: FmSpace.x3,
      ),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(FmRadius.lg),
        borderSide: const BorderSide(color: FmColors.borderMuted),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(FmRadius.lg),
        borderSide: const BorderSide(color: FmColors.accent, width: 1.2),
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
        backgroundColor: FmColors.textPrimary,
        foregroundColor: Colors.white,
        elevation: 0,
        shape: const StadiumBorder(),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        foregroundColor: FmColors.textPrimary,
        side: const BorderSide(color: FmColors.borderMuted),
        shape: const StadiumBorder(),
      ),
    ),
    listTileTheme: const ListTileThemeData(iconColor: FmColors.textSecondary),
    dividerTheme: const DividerThemeData(
      color: FmColors.borderMuted,
      space: 1,
      thickness: 1,
    ),
  );
}

ThemeData buildForkMeshDarkTheme() {
  const scheme = ColorScheme.dark(
    primary: FmColors.darkTextPrimary,
    secondary: FmColors.accentOnDark,
    surface: FmColors.darkBgRaised,
    error: FmColors.dangerOnDark,
    onPrimary: FmColors.darkBgBase,
    onSurface: FmColors.darkTextPrimary,
  );
  return ThemeData(
    useMaterial3: true,
    brightness: Brightness.dark,
    colorScheme: scheme,
    scaffoldBackgroundColor: FmColors.darkBgBase,
    canvasColor: FmColors.darkBgBase,
    dividerColor: FmColors.darkBorder,
    textTheme: _forkMeshTextTheme(Brightness.dark),
    appBarTheme: const AppBarTheme(
      backgroundColor: FmColors.darkBgRaised,
      surfaceTintColor: Colors.transparent,
      foregroundColor: FmColors.darkTextPrimary,
      elevation: 0,
    ),
    cardTheme: const CardThemeData(
      color: FmColors.darkBgRaised,
      elevation: 0,
      margin: EdgeInsets.zero,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.all(Radius.circular(FmRadius.md)),
      ),
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: FmColors.darkBgRaised,
      hintStyle: const TextStyle(color: FmColors.darkTextTertiary),
      labelStyle: const TextStyle(color: FmColors.darkTextSecondary),
      contentPadding: const EdgeInsets.symmetric(
        horizontal: FmSpace.x4,
        vertical: FmSpace.x3,
      ),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(FmRadius.lg),
        borderSide: const BorderSide(color: FmColors.darkBorder),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(FmRadius.lg),
        borderSide: const BorderSide(color: FmColors.accentOnDark, width: 1.2),
      ),
    ),
    elevatedButtonTheme: ElevatedButtonThemeData(
      style: ElevatedButton.styleFrom(
        backgroundColor: FmColors.darkTextPrimary,
        foregroundColor: FmColors.darkBgBase,
        elevation: 0,
        shape: const StadiumBorder(),
      ),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        backgroundColor: FmColors.darkTextPrimary,
        foregroundColor: FmColors.darkBgBase,
        elevation: 0,
        shape: const StadiumBorder(),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        foregroundColor: FmColors.darkTextPrimary,
        side: const BorderSide(color: FmColors.darkBorder),
        shape: const StadiumBorder(),
      ),
    ),
    listTileTheme: const ListTileThemeData(
      iconColor: FmColors.darkTextSecondary,
    ),
    dividerTheme: const DividerThemeData(
      color: FmColors.darkBorder,
      space: 1,
      thickness: 1,
    ),
  );
}

ThemeData buildForkMeshTheme() => buildForkMeshLightTheme();

TextTheme _forkMeshTextTheme(Brightness brightness) {
  final color = brightness == Brightness.dark
      ? FmColors.darkTextPrimary
      : FmColors.textPrimary;
  final muted = brightness == Brightness.dark
      ? FmColors.darkTextSecondary
      : FmColors.textSecondary;
  final base = Typography.material2021(
    platform: TargetPlatform.iOS,
  ).black.apply(bodyColor: color, displayColor: color);
  return base.copyWith(
    titleLarge: base.titleLarge?.copyWith(
      fontSize: 22,
      fontWeight: FontWeight.w800,
      letterSpacing: 0,
    ),
    titleMedium: base.titleMedium?.copyWith(
      fontSize: 17,
      fontWeight: FontWeight.w800,
      letterSpacing: 0,
    ),
    titleSmall: base.titleSmall?.copyWith(
      fontSize: 15,
      fontWeight: FontWeight.w700,
      letterSpacing: 0,
      color: muted,
    ),
    bodyLarge: base.bodyLarge?.copyWith(
      fontSize: 16,
      fontWeight: FontWeight.w500,
      letterSpacing: 0,
    ),
    bodyMedium: base.bodyMedium?.copyWith(
      fontSize: 14,
      fontWeight: FontWeight.w500,
      letterSpacing: 0,
    ),
    bodySmall: base.bodySmall?.copyWith(
      fontSize: 12,
      fontWeight: FontWeight.w600,
      letterSpacing: 0,
      color: muted,
    ),
    labelLarge: base.labelLarge?.copyWith(
      fontWeight: FontWeight.w800,
      letterSpacing: 0,
    ),
    labelMedium: base.labelMedium?.copyWith(
      fontWeight: FontWeight.w700,
      letterSpacing: 0,
    ),
    labelSmall: base.labelSmall?.copyWith(
      fontWeight: FontWeight.w700,
      letterSpacing: 0,
    ),
  );
}
