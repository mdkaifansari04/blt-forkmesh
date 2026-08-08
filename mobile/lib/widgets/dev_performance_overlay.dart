import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../services/performance_monitor_service.dart';
import '../theme.dart';
import 'fm_ui.dart';

class DevPerformanceOverlay extends StatefulWidget {
  const DevPerformanceOverlay({
    super.key,
    required this.monitor,
    required this.child,
  });

  final PerformanceMonitorService monitor;
  final Widget child;

  @override
  State<DevPerformanceOverlay> createState() => _DevPerformanceOverlayState();
}

class _DevPerformanceOverlayState extends State<DevPerformanceOverlay> {
  final _stackKey = GlobalKey();
  final _indicatorMeasureKey = GlobalKey();

  Offset? _indicatorOffset;
  Offset? _dragStartGlobalPosition;
  Offset? _dragStartIndicatorOffset;
  bool _showLogPopup = false;

  @override
  Widget build(BuildContext context) {
    if (!widget.monitor.enabled) return widget.child;
    return AnimatedBuilder(
      animation: widget.monitor,
      builder: (context, _) {
        final event = widget.monitor.latestEvent;
        return LayoutBuilder(
          builder: (context, constraints) {
            return Stack(
              key: _stackKey,
              children: [
                widget.child,
                if (event != null)
                  _buildDraggableIndicator(
                    context: context,
                    event: event,
                    bounds: constraints.biggest,
                  ),
                if (_showLogPopup) _buildLogPopup(context),
              ],
            );
          },
        );
      },
    );
  }

  Widget _buildDraggableIndicator({
    required BuildContext context,
    required PerformanceEvent event,
    required Size bounds,
  }) {
    final padding = MediaQuery.paddingOf(context);
    final indicatorSize = _indicatorSize();
    final position = _indicatorOffset;
    final child = GestureDetector(
      key: const ValueKey('dev-performance-indicator'),
      behavior: HitTestBehavior.translucent,
      onTap: _showDetails,
      onPanDown: (details) {
        _dragStartGlobalPosition = details.globalPosition;
        _dragStartIndicatorOffset = _currentIndicatorOffset();
      },
      onPanUpdate: (details) {
        _moveIndicator(details: details, bounds: bounds, padding: padding);
      },
      onPanEnd: (_) => _clearDragStart(),
      onPanCancel: _clearDragStart,
      child: KeyedSubtree(
        key: _indicatorMeasureKey,
        child: _PerformanceIndicator(event: event),
      ),
    );

    if (position == null) {
      return Positioned(
        top: padding.top + FmSpace.x2,
        right: FmSpace.x3,
        child: child,
      );
    }

    final clamped = _clampIndicatorOffset(
      position,
      bounds,
      indicatorSize,
      padding,
    );
    return Positioned(left: clamped.dx, top: clamped.dy, child: child);
  }

  void _moveIndicator({
    required DragUpdateDetails details,
    required Size bounds,
    required EdgeInsets padding,
  }) {
    final dragStartGlobal =
        _dragStartGlobalPosition ?? details.globalPosition - details.delta;
    final dragStartIndicator =
        _dragStartIndicatorOffset ?? _currentIndicatorOffset();
    final next =
        dragStartIndicator + (details.globalPosition - dragStartGlobal);
    setState(() {
      _indicatorOffset = _clampIndicatorOffset(
        next,
        bounds,
        _indicatorSize(),
        padding,
      );
    });
  }

  void _clearDragStart() {
    _dragStartGlobalPosition = null;
    _dragStartIndicatorOffset = null;
  }

  Offset _currentIndicatorOffset() {
    final stack = _stackKey.currentContext?.findRenderObject() as RenderBox?;
    final indicator =
        _indicatorMeasureKey.currentContext?.findRenderObject() as RenderBox?;
    if (stack == null || indicator == null) {
      return _indicatorOffset ?? Offset.zero;
    }
    return stack.globalToLocal(indicator.localToGlobal(Offset.zero));
  }

  Size _indicatorSize() {
    final indicator =
        _indicatorMeasureKey.currentContext?.findRenderObject() as RenderBox?;
    return indicator?.size ?? Size.zero;
  }

  Offset _clampIndicatorOffset(
    Offset offset,
    Size bounds,
    Size indicatorSize,
    EdgeInsets padding,
  ) {
    const margin = FmSpace.x2;
    final minX = margin;
    final maxX = (bounds.width - indicatorSize.width - margin)
        .clamp(minX, double.infinity)
        .toDouble();
    final minY = padding.top + margin;
    final maxY =
        (bounds.height - indicatorSize.height - padding.bottom - margin)
            .clamp(minY, double.infinity)
            .toDouble();

    return Offset(
      offset.dx.clamp(minX, maxX).toDouble(),
      offset.dy.clamp(minY, maxY).toDouble(),
    );
  }

  Widget _buildLogPopup(BuildContext context) {
    return Positioned.fill(
      child: Stack(
        children: [
          ModalBarrier(
            color: Colors.black.withValues(alpha: 0.38),
            dismissible: true,
            onDismiss: _hideDetails,
          ),
          SafeArea(
            child: Padding(
              padding: const EdgeInsets.all(FmSpace.x4),
              child: Center(
                child: _PerformanceDialog(
                  monitor: widget.monitor,
                  onCopy: _copyPerformanceLog,
                  onClose: _hideDetails,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  void _showDetails() {
    setState(() => _showLogPopup = true);
  }

  void _hideDetails() {
    setState(() => _showLogPopup = false);
  }

  Future<void> _copyPerformanceLog() async {
    await Clipboard.setData(
      ClipboardData(text: _formatPerformanceLog(widget.monitor.events)),
    );
  }
}

class _PerformanceIndicator extends StatelessWidget {
  const _PerformanceIndicator({required this.event});

  final PerformanceEvent event;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      label: 'Open performance monitor',
      child: Material(
        color: FmTheme.warning(context),
        borderRadius: BorderRadius.circular(FmRadius.full),
        elevation: 6,
        child: Padding(
          padding: const EdgeInsets.symmetric(
            horizontal: FmSpace.x3,
            vertical: FmSpace.x2,
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Icon(Icons.speed_rounded, size: 16, color: Colors.white),
              const SizedBox(width: FmSpace.x1),
              Text(
                '${event.durationMs}ms',
                style: const TextStyle(
                  color: Colors.white,
                  fontSize: 12,
                  fontWeight: FontWeight.w800,
                  height: 1,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _PerformanceDialog extends StatelessWidget {
  const _PerformanceDialog({
    required this.monitor,
    required this.onCopy,
    required this.onClose,
  });

  final PerformanceMonitorService monitor;
  final VoidCallback onCopy;
  final VoidCallback onClose;

  @override
  Widget build(BuildContext context) {
    final size = MediaQuery.sizeOf(context);
    return Material(
      key: const ValueKey('dev-performance-log-popup'),
      color: Colors.transparent,
      child: Container(
        constraints: BoxConstraints(
          maxWidth: 640,
          maxHeight: size.height * 0.78,
        ),
        decoration: BoxDecoration(
          color: FmTheme.bgRaised(context),
          borderRadius: BorderRadius.circular(FmRadius.lg),
          border: Border.all(color: FmTheme.border(context)),
          boxShadow: [
            BoxShadow(
              color: Colors.black.withValues(alpha: 0.22),
              blurRadius: 28,
              offset: const Offset(0, 18),
            ),
          ],
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(
                FmSpace.x4,
                FmSpace.x4,
                FmSpace.x3,
                FmSpace.x2,
              ),
              child: Row(
                children: [
                  Expanded(
                    child: Text(
                      'Performance monitor',
                      style: TextStyle(
                        color: FmTheme.textPrimary(context),
                        fontSize: 20,
                        fontWeight: FontWeight.w700,
                        height: 1.1,
                      ),
                    ),
                  ),
                  TextButton.icon(
                    onPressed: monitor.clear,
                    icon: const Icon(Icons.delete_outline),
                    label: const Text('Clear'),
                  ),
                ],
              ),
            ),
            Flexible(
              child: AnimatedBuilder(
                animation: monitor,
                builder: (context, _) {
                  final events = monitor.events;
                  if (events.isEmpty) {
                    return const FmEmptyState(
                      icon: Icons.speed_rounded,
                      title: 'No slow events',
                      message:
                          'Operations and frames over 500 ms will appear here.',
                    );
                  }
                  return ListView.separated(
                    padding: const EdgeInsets.fromLTRB(
                      FmSpace.x4,
                      FmSpace.x1,
                      FmSpace.x4,
                      FmSpace.x5,
                    ),
                    itemCount: events.length,
                    separatorBuilder: (_, _) =>
                        const SizedBox(height: FmSpace.x2),
                    itemBuilder: (context, index) =>
                        _PerformanceEventCard(event: events[index]),
                  );
                },
              ),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(
                FmSpace.x4,
                FmSpace.x2,
                FmSpace.x4,
                FmSpace.x4,
              ),
              child: Row(
                children: [
                  TextButton.icon(
                    onPressed: onCopy,
                    icon: const Icon(Icons.copy_rounded),
                    label: const Text('Copy'),
                  ),
                  const Spacer(),
                  FilledButton.icon(
                    onPressed: onClose,
                    icon: const Icon(Icons.close_rounded),
                    label: const Text('Close'),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

String _formatPerformanceLog(List<PerformanceEvent> events) {
  if (events.isEmpty) return 'No slow performance events recorded.';
  return events.map(_formatPerformanceEvent).join('\n\n');
}

String _formatPerformanceEvent(PerformanceEvent event) {
  final lines = <String>[
    '[${event.kind.name}] ${event.name}',
    'duration: ${event.durationMs}ms',
    'timestamp: ${event.timestamp.toIso8601String()}',
  ];

  for (final entry in event.details.entries) {
    lines.add('${entry.key}: ${entry.value}');
  }

  if (event.stackTrace.isNotEmpty) {
    lines.add('stack:');
    lines.add(event.stackTrace);
  }

  return lines.join('\n');
}

class _PerformanceEventCard extends StatelessWidget {
  const _PerformanceEventCard({required this.event});

  final PerformanceEvent event;

  @override
  Widget build(BuildContext context) {
    final details = event.details.entries
        .map((entry) => '${entry.key}: ${entry.value}')
        .join('\n');
    final timestamp =
        '${event.timestamp.hour.toString().padLeft(2, '0')}:'
        '${event.timestamp.minute.toString().padLeft(2, '0')}:'
        '${event.timestamp.second.toString().padLeft(2, '0')}';

    return FmCard(
      radius: FmRadius.lg,
      color: FmTheme.bgBase(context),
      padding: const EdgeInsets.all(FmSpace.x3),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                event.kind == PerformanceEventKind.frame
                    ? Icons.layers_outlined
                    : Icons.route_outlined,
                color: FmTheme.warning(context),
                size: 18,
              ),
              const SizedBox(width: FmSpace.x2),
              Expanded(
                child: Text(
                  event.name,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: FmTheme.textPrimary(context),
                    fontSize: 15,
                    fontWeight: FontWeight.w800,
                  ),
                ),
              ),
              const SizedBox(width: FmSpace.x2),
              FmStatusBadge(
                label: '${event.durationMs}ms',
                color: FmTheme.warning(context),
              ),
            ],
          ),
          const SizedBox(height: FmSpace.x2),
          Text(
            '${event.kind.name} at $timestamp',
            style: TextStyle(
              color: FmTheme.textTertiary(context),
              fontSize: 12,
              fontWeight: FontWeight.w600,
            ),
          ),
          if (details.isNotEmpty) ...[
            const SizedBox(height: FmSpace.x2),
            Text(
              details,
              style: TextStyle(
                color: FmTheme.textSecondary(context),
                fontSize: 12,
                height: 1.35,
              ),
            ),
          ],
          if (event.stackTrace.isNotEmpty) ...[
            const SizedBox(height: FmSpace.x2),
            SelectableText(
              event.stackTrace,
              style: TextStyle(
                color: FmTheme.textTertiary(context),
                fontFamily: 'monospace',
                fontSize: 10,
                height: 1.25,
              ),
            ),
          ],
        ],
      ),
    );
  }
}
