import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/theme.dart';
import 'package:forkmesh/widgets/fm_ui.dart';

void main() {
  test('ForkMesh design tokens expose the Lunel-inspired spacing layers', () {
    expect(FmSpace.x0, 0);
    expect(FmSpace.x1, 4);
    expect(FmSpace.x2, 8);
    expect(FmSpace.x3, 12);
    expect(FmSpace.x4, 16);
    expect(FmSpace.x5, 24);
    expect(FmSpace.x6, 32);
    expect(FmSpace.x7, 48);
    expect(FmSpace.x8, 64);

    expect(FmRadius.sm, 4);
    expect(FmRadius.md, 8);
    expect(FmRadius.lg, 12);
    expect(FmRadius.full, 999);

    expect(FmMotion.fast, const Duration(milliseconds: 100));
    expect(FmMotion.normal, const Duration(milliseconds: 200));
    expect(FmColors.bgBase, const Color(0xFFF7F7F7));
    expect(FmColors.bgRaised, Colors.white);
    expect(FmColors.bgOverlay, const Color(0xFFF0F0F0));
    expect(FmColors.bgElevated, const Color(0xFFE8E8E8));
    expect(FmColors.textPrimary, const Color(0xFF0A0A0A));
    expect(FmColors.accentSubtle, const Color(0x1A6161F2));
  });

  test('ForkMesh exposes paired light and dark app themes', () {
    final light = buildForkMeshLightTheme();
    final dark = buildForkMeshDarkTheme();

    expect(light.brightness, Brightness.light);
    expect(dark.brightness, Brightness.dark);
    expect(light.scaffoldBackgroundColor, isNot(dark.scaffoldBackgroundColor));
    expect(dark.colorScheme.surface, const Color(0xFF141414));
  });

  testWidgets('FmCard defaults to a raised borderless layer', (tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(body: FmCard(child: Text('Raised'))),
      ),
    );

    final material = tester.widget<Material>(
      find.descendant(of: find.byType(FmCard), matching: find.byType(Material)),
    );
    final borderRadius = material.borderRadius! as BorderRadius;

    expect(material.color, FmColors.bgRaised);
    expect(borderRadius.topLeft.x, FmRadius.md);
    expect(material.shape, isNull);
    expect(find.text('Raised'), findsOneWidget);
  });

  testWidgets('FmStatusBadge uses semantic tint without a border', (
    tester,
  ) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: FmStatusBadge(label: 'public', color: FmColors.success),
        ),
      ),
    );

    final container = tester.widget<Container>(
      find
          .ancestor(of: find.text('PUBLIC'), matching: find.byType(Container))
          .first,
    );
    final decoration = container.decoration! as BoxDecoration;

    expect(find.text('PUBLIC'), findsOneWidget);
    expect(decoration.border, isNull);
    expect(decoration.borderRadius, isA<BorderRadius>());
  });
}
