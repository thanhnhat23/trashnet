import 'package:flutter_test/flutter_test.dart';
import 'package:trashnet/core/standalone/classifier.dart';

void main() {
  test('decide rejects weak or ambiguous predictions to other', () {
    expect(decide([0.9, 0.05, 0.03, 0.02], 0.45, 0.1).label, 'paper');
    expect(decide([0.4, 0.3, 0.2, 0.1], 0.45, 0.1).label, 'other');
    expect(decide([0.5, 0.45, 0.03, 0.02], 0.45, 0.1).uncertain, true);
  });
  test('temperature and vote keep a distribution', () {
    final p = applyTemperature([0.7, 0.2, 0.05, 0.05], 2);
    expect(p.reduce((a, b) => a + b), closeTo(1, 1e-9));
    final v = vote([[1, 0, 0, 0], [0, 1, 0, 0]], [3, 1]);
    expect(v[0], closeTo(0.75, 1e-9));
  });
}
