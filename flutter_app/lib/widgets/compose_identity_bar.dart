import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/auth_service.dart';
import '../theme.dart';

/// A compact "you are posting as <username>" header shown above every compose
/// surface (chat, issues, PRs, discussions, reviews, comments). Renders the
/// account's real avatar (base64 PNG) when present, otherwise an initials
/// circle matching [AvatarWithDot].
class ComposeIdentityBar extends StatelessWidget {
  const ComposeIdentityBar({super.key, this.verb, this.padding});

  /// Optional leading verb, e.g. "Posting", "Replying", "Reviewing". When null
  /// the bar just shows the account name.
  final String? verb;
  final EdgeInsetsGeometry? padding;

  @override
  Widget build(BuildContext context) {
    final session = context.watch<AuthService?>()?.session;
    if (session == null) return const SizedBox.shrink();

    final name = session.nodeName.isNotEmpty ? session.nodeName : session.email;
    final label = verb == null || verb!.isEmpty ? 'as ' : '${verb!} as ';

    return Padding(
      padding: padding ?? const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          _ComposeAvatar(name: name, avatarPng: session.avatarPng),
          const SizedBox(width: 8),
          Flexible(
            child: RichText(
              overflow: TextOverflow.ellipsis,
              text: TextSpan(
                style: TextStyle(
                  fontSize: 13,
                  color: FmTheme.textSecondary(context),
                ),
                children: [
                  TextSpan(text: label),
                  TextSpan(
                    text: name,
                    style: TextStyle(
                      fontWeight: FontWeight.w700,
                      color: FmTheme.textPrimary(context),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _ComposeAvatar extends StatelessWidget {
  const _ComposeAvatar({required this.name, required this.avatarPng});

  final String name;
  final String avatarPng;

  @override
  Widget build(BuildContext context) {
    if (avatarPng.isNotEmpty) {
      try {
        final bytes = base64Decode(avatarPng);
        return CircleAvatar(radius: 14, backgroundImage: MemoryImage(bytes));
      } catch (_) {
        // Fall through to the initials avatar below.
      }
    }
    final initial = name.isEmpty ? '?' : name.characters.first.toUpperCase();
    return CircleAvatar(
      radius: 14,
      backgroundColor: FmColors.senderColor(name),
      child: Text(
        initial,
        style: const TextStyle(
          color: Colors.white,
          fontWeight: FontWeight.w700,
          fontSize: 13,
        ),
      ),
    );
  }
}
