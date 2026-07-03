import 'dart:ui' show FrameTiming, TimingsCallback;

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

enum PerformanceEventKind { operation, frame }

class PerformanceEvent {
  const PerformanceEvent({
    required this.kind,
    required this.name,
    required this.duration,
    required this.timestamp,
    this.details = const {},
    this.stackTrace = '',
  });

  final PerformanceEventKind kind;
  final String name;
  final Duration duration;
  final DateTime timestamp;
  final Map<String, Object?> details;
  final String stackTrace;

  int get durationMs => duration.inMilliseconds;
}

class PerformanceMonitorService extends ChangeNotifier {
  PerformanceMonitorService({
    bool? enabled,
    this.threshold = const Duration(milliseconds: 500),
    this.maxEvents = 100,
    this.logSink,
    DateTime Function()? now,
    Duration Function()? elapsedForTest,
  }) : enabled = enabled ?? kDebugMode,
       _now = now ?? DateTime.now,
       _elapsedForTest = elapsedForTest;

  final bool enabled;
  final Duration threshold;
  final int maxEvents;
  final void Function(String message)? logSink;
  final DateTime Function() _now;
  final Duration Function()? _elapsedForTest;

  final List<PerformanceEvent> _events = [];
  TimingsCallback? _timingsCallback;

  List<PerformanceEvent> get events => List.unmodifiable(_events);
  bool get hasEvents => _events.isNotEmpty;
  PerformanceEvent? get latestEvent => _events.isEmpty ? null : _events.first;

  Future<T> track<T>(
    String name,
    Future<T> Function() action, {
    Map<String, Object?> details = const {},
  }) async {
    if (!enabled) return action();

    final callStack = StackTrace.current;
    final stopwatch = Stopwatch()..start();
    Object? error;
    StackTrace? errorStack;

    try {
      return await action();
    } catch (e, st) {
      error = e;
      errorStack = st;
      rethrow;
    } finally {
      stopwatch.stop();
      final elapsed = _elapsedForTest?.call() ?? stopwatch.elapsed;
      if (elapsed >= threshold) {
        recordSlowEvent(
          kind: PerformanceEventKind.operation,
          name: name,
          duration: elapsed,
          details: {...details, if (error != null) 'error': '$error'},
          stackTrace: _formatStack(errorStack ?? callStack),
        );
      }
    }
  }

  void installFrameTimingMonitor(WidgetsBinding binding) {
    if (!enabled || _timingsCallback != null) return;
    _timingsCallback = _handleFrameTimings;
    binding.addTimingsCallback(_timingsCallback!);
  }

  void uninstallFrameTimingMonitor(WidgetsBinding binding) {
    final callback = _timingsCallback;
    if (callback == null) return;
    binding.removeTimingsCallback(callback);
    _timingsCallback = null;
  }

  void _handleFrameTimings(List<FrameTiming> timings) {
    for (final timing in timings) {
      recordFrameDelay(
        buildDuration: timing.buildDuration,
        rasterDuration: timing.rasterDuration,
      );
    }
  }

  void recordFrameDelay({
    required Duration buildDuration,
    required Duration rasterDuration,
  }) {
    if (!enabled) return;
    final total = buildDuration + rasterDuration;
    if (total < threshold) return;
    recordSlowEvent(
      kind: PerformanceEventKind.frame,
      name: 'frame',
      duration: total,
      details: {
        'buildMs': buildDuration.inMilliseconds,
        'rasterMs': rasterDuration.inMilliseconds,
      },
    );
  }

  void recordSlowEvent({
    required PerformanceEventKind kind,
    required String name,
    required Duration duration,
    Map<String, Object?> details = const {},
    String stackTrace = '',
  }) {
    if (!enabled || duration < threshold) return;

    final event = PerformanceEvent(
      kind: kind,
      name: name,
      duration: duration,
      timestamp: _now(),
      details: Map.unmodifiable(details),
      stackTrace: stackTrace,
    );
    _events.insert(0, event);
    if (_events.length > maxEvents) {
      _events.removeRange(maxEvents, _events.length);
    }
    _log(event);
    notifyListeners();
  }

  void clear() {
    if (_events.isEmpty) return;
    _events.clear();
    notifyListeners();
  }

  void _log(PerformanceEvent event) {
    final sink = logSink ?? debugPrint;
    final details = event.details.entries
        .map((entry) => '${entry.key}=${entry.value}')
        .join(' ');
    final detailSuffix = details.isEmpty ? '' : ' $details';
    sink(
      '[perf] ${event.kind.name} ${event.name} ${event.durationMs}ms$detailSuffix',
    );
  }

  String _formatStack(StackTrace stackTrace) {
    return stackTrace.toString().split('\n').take(8).join('\n');
  }
}
