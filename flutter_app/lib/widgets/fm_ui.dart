import 'package:flutter/material.dart';

import '../theme.dart';

class FmCard extends StatelessWidget {
  const FmCard({
    super.key,
    this.child,
    this.onTap,
    this.color,
    this.padding,
    this.radius,
    this.margin,
  });

  final Widget? child;
  final VoidCallback? onTap;
  final Color? color;
  final EdgeInsetsGeometry? padding;
  final double? radius;
  final EdgeInsetsGeometry? margin;

  @override
  Widget build(BuildContext context) {
    final borderRadius = BorderRadius.circular(radius ?? FmRadius.md);
    final content = Padding(
      padding: padding ?? const EdgeInsets.all(FmSpace.x4),
      child: child ?? const SizedBox.shrink(),
    );
    final material = Material(
      color: color ?? FmTheme.bgRaised(context),
      borderRadius: borderRadius,
      clipBehavior: Clip.antiAlias,
      child: onTap == null
          ? content
          : InkWell(onTap: onTap, borderRadius: borderRadius, child: content),
    );

    if (margin == null) return material;
    return Padding(padding: margin!, child: material);
  }
}

class FmSectionHeader extends StatelessWidget {
  const FmSectionHeader({
    super.key,
    required this.title,
    this.icon,
    this.trailing,
    this.count,
  });

  final String title;
  final IconData? icon;
  final Widget? trailing;
  final int? count;

  @override
  Widget build(BuildContext context) {
    final trailingWidget =
        trailing ??
        (count == null
            ? null
            : Text(
                '$count',
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 12,
                  fontWeight: FontWeight.w700,
                ),
              ));

    return Row(
      children: [
        if (icon != null) ...[
          Icon(icon, color: FmTheme.textTertiary(context), size: 14),
          const SizedBox(width: FmSpace.x2),
        ],
        Expanded(
          child: Text(
            title.toUpperCase(),
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(
              color: FmTheme.textTertiary(context),
              fontSize: 12,
              fontWeight: FontWeight.w700,
              letterSpacing: 0.5,
            ),
          ),
        ),
        if (trailingWidget != null) ...[
          const SizedBox(width: FmSpace.x3),
          trailingWidget,
        ],
      ],
    );
  }
}

class FmStatusBadge extends StatelessWidget {
  const FmStatusBadge({
    super.key,
    required this.label,
    required this.color,
    this.backgroundColor,
  });

  final String label;
  final Color color;
  final Color? backgroundColor;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(
        horizontal: FmSpace.x2,
        vertical: FmSpace.x1,
      ),
      decoration: BoxDecoration(
        color: backgroundColor ?? color.withAlpha(31),
        borderRadius: BorderRadius.circular(FmRadius.full),
      ),
      child: Text(
        label.toUpperCase(),
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
        style: TextStyle(
          color: color,
          fontSize: 11,
          fontWeight: FontWeight.w800,
          letterSpacing: 0.5,
          height: 1,
        ),
      ),
    );
  }
}

class FmEmptyState extends StatelessWidget {
  const FmEmptyState({
    super.key,
    required this.icon,
    required this.title,
    this.message,
  });

  final IconData icon;
  final String title;
  final String? message;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.symmetric(
          horizontal: FmSpace.x5,
          vertical: FmSpace.x7,
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 40, color: FmTheme.textTertiary(context)),
            const SizedBox(height: FmSpace.x4),
            Text(
              title,
              textAlign: TextAlign.center,
              style: TextStyle(
                color: FmTheme.textPrimary(context),
                fontSize: 17,
                fontWeight: FontWeight.w700,
              ),
            ),
            if (message != null) ...[
              const SizedBox(height: FmSpace.x2),
              Text(
                message!,
                textAlign: TextAlign.center,
                style: TextStyle(
                  color: FmTheme.textSecondary(context),
                  fontSize: 14,
                  height: 1.35,
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class FmBottomActionBar extends StatelessWidget {
  const FmBottomActionBar({super.key, required this.child, this.padding});

  final Widget child;
  final EdgeInsetsGeometry? padding;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: FmTheme.bgRaised(context),
      child: SafeArea(
        top: false,
        child: Padding(
          padding: padding ?? const EdgeInsets.all(FmSpace.x4),
          child: child,
        ),
      ),
    );
  }
}

class FmPanelHeader extends StatelessWidget {
  const FmPanelHeader({
    super.key,
    required this.title,
    this.subtitle,
    this.leading,
    this.trailing,
  });

  final String title;
  final String? subtitle;
  final Widget? leading;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 56,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: FmSpace.x4),
        child: Row(
          children: [
            if (leading != null) ...[
              leading!,
              const SizedBox(width: FmSpace.x3),
            ],
            Expanded(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      color: FmTheme.textPrimary(context),
                      fontSize: 17,
                      fontWeight: FontWeight.w600,
                      height: 1.15,
                    ),
                  ),
                  if (subtitle != null) ...[
                    const SizedBox(height: FmSpace.x1),
                    Text(
                      subtitle!,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        color: FmTheme.textSecondary(context),
                        fontSize: 12,
                        height: 1.2,
                      ),
                    ),
                  ],
                ],
              ),
            ),
            if (trailing != null) ...[
              const SizedBox(width: FmSpace.x3),
              trailing!,
            ],
          ],
        ),
      ),
    );
  }
}

class FmKeyValueRow extends StatelessWidget {
  const FmKeyValueRow({super.key, required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: FmSpace.x2),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 96,
            child: Text(
              label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                color: FmTheme.textTertiary(context),
                fontFamily: 'monospace',
                fontSize: 12,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),
          const SizedBox(width: FmSpace.x4),
          Expanded(
            child: Text(
              value,
              style: TextStyle(
                color: FmTheme.textPrimary(context),
                fontSize: 14,
                height: 1.3,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
