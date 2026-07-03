import 'package:flutter/material.dart';

import '../services/relay_service.dart';
import '../theme.dart';

/// Small avatar with a presence dot, mirroring the Qt client's top-right avatar
/// + connection dot (green online / amber connecting / grey offline).
class AvatarWithDot extends StatelessWidget {
  const AvatarWithDot({
    super.key,
    required this.state,
    required this.label,
    this.size = 34,
  });

  final RelayConnectionState state;
  final String label;
  final double size;

  Color get _dotColor => switch (state) {
    RelayConnectionState.connected => FmColors.success,
    RelayConnectionState.connecting => FmColors.warning,
    RelayConnectionState.offline => FmColors.offline,
  };

  @override
  Widget build(BuildContext context) {
    final initial = label.isEmpty ? '?' : label.characters.first.toUpperCase();
    return SizedBox(
      width: size,
      height: size,
      child: Stack(
        clipBehavior: Clip.none,
        children: [
          CircleAvatar(
            radius: size / 2,
            backgroundColor: FmColors.senderColor(label),
            child: Text(
              initial,
              style: const TextStyle(
                color: Colors.white,
                fontWeight: FontWeight.w700,
              ),
            ),
          ),
          Positioned(
            right: -1,
            bottom: -1,
            child: Container(
              width: 12,
              height: 12,
              decoration: BoxDecoration(
                color: _dotColor,
                shape: BoxShape.circle,
                border: Border.all(color: FmColors.bgRaised, width: 2),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
