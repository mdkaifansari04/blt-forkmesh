import 'package:flutter/widgets.dart';

/// Icons vendored into the app so we carry no third-party icon packages.
///
/// The glyphs come from the MingCute icon set
/// (https://github.com/Richard9394/MingCute, Apache-2.0; see
/// assets/fonts/MingCute-LICENSE). The font asset is subset to only the
/// codepoints listed here — when adding an icon, regenerate the subset:
///   pyftsubset MingCute.ttf --unicodes=U+F127,... --no-layout-closure \
///     --output-file=assets/fonts/MingCuteNotification.ttf
class FmIcons {
  FmIcons._();

  static const String _fontFamily = 'MingCute';

  /// MingCute `notification_line` bell outline.
  static const IconData notificationLine = IconData(
    0xf127,
    fontFamily: _fontFamily,
  );
}
