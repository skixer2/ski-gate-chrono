import 'package:flutter_test/flutter_test.dart';
import 'dart:math';
import 'package:sgc_phone/models/vec3.dart';

void main() {
  group('Vec3', () {
    group('addition', () {
      test('adds two vectors component-wise', () {
        final a = Vec3(1, 2, 3);
        final b = Vec3(4, 5, 6);
        final c = a + b;
        expect(c.x, closeTo(5, 1e-9));
        expect(c.y, closeTo(7, 1e-9));
        expect(c.z, closeTo(9, 1e-9));
      });

      test('adds zero vector (identity)', () {
        final a = Vec3(3.5, -2.1, 7.8);
        final z = Vec3(0, 0, 0);
        final c = a + z;
        expect(c.x, closeTo(a.x, 1e-9));
        expect(c.y, closeTo(a.y, 1e-9));
        expect(c.z, closeTo(a.z, 1e-9));
      });
    });

    group('subtraction', () {
      test('subtracts two vectors', () {
        final a = Vec3(10, 8, 6);
        final b = Vec3(3, 5, 7);
        final c = a - b;
        expect(c.x, closeTo(7, 1e-9));
        expect(c.y, closeTo(3, 1e-9));
        expect(c.z, closeTo(-1, 1e-9));
      });
    });

    group('scalar multiplication', () {
      test('multiplies by positive scalar', () {
        final a = Vec3(1, 2, 3);
        final b = a * 2.0;
        expect(b.x, closeTo(2, 1e-9));
        expect(b.y, closeTo(4, 1e-9));
        expect(b.z, closeTo(6, 1e-9));
      });

      test('multiplies by negative scalar', () {
        final a = Vec3(1, -2, 3);
        final b = a * -1.0;
        expect(b.x, closeTo(-1, 1e-9));
        expect(b.y, closeTo(2, 1e-9));
        expect(b.z, closeTo(-3, 1e-9));
      });

      test('multiplies by zero', () {
        final a = Vec3(5, 10, 15);
        final b = a * 0.0;
        expect(b.x, closeTo(0, 1e-9));
        expect(b.y, closeTo(0, 1e-9));
        expect(b.z, closeTo(0, 1e-9));
      });
    });

    group('dot product', () {
      test('orthogonal vectors = 0', () {
        expect(Vec3(1, 0, 0).dot(Vec3(0, 1, 0)), closeTo(0, 1e-9));
        expect(Vec3(1, 0, 0).dot(Vec3(0, 0, 1)), closeTo(0, 1e-9));
      });

      test('parallel vectors = product of lengths', () {
        final a = Vec3(3, 0, 0);
        final b = Vec3(2, 0, 0);
        expect(a.dot(b), closeTo(6, 1e-9));
      });

      test('general case', () {
        expect(Vec3(1, 2, 3).dot(Vec3(4, -5, 6)), closeTo(12, 1e-9));
        // 1*4 + 2*(-5) + 3*6 = 4 - 10 + 18 = 12
      });
    });

    group('cross product', () {
      test('i × j = k', () {
        final c = Vec3(1, 0, 0).cross(Vec3(0, 1, 0));
        expect(c.x, closeTo(0, 1e-9));
        expect(c.y, closeTo(0, 1e-9));
        expect(c.z, closeTo(1, 1e-9));
      });

      test('j × i = -k', () {
        final c = Vec3(0, 1, 0).cross(Vec3(1, 0, 0));
        expect(c.z, closeTo(-1, 1e-9));
      });

      test('cross product is orthogonal to both inputs', () {
        final a = Vec3(1, 2, 3);
        final b = Vec3(4, 5, 6);
        final c = a.cross(b);
        expect(a.dot(c), closeTo(0, 1e-6));
        expect(b.dot(c), closeTo(0, 1e-6));
      });
    });

    group('length', () {
      test('unit vector length = 1', () {
        expect(Vec3(1, 0, 0).length, closeTo(1, 1e-9));
        expect(Vec3(0, 1, 0).length, closeTo(1, 1e-9));
      });

      test('3-4-5 triangle', () {
        expect(Vec3(3, 4, 0).length, closeTo(5, 1e-9));
      });

      test('zero vector', () {
        expect(Vec3(0, 0, 0).length, closeTo(0, 1e-9));
      });
    });

    group('normalized', () {
      test('normalizes to unit length', () {
        final n = Vec3(3, 4, 0).normalized();
        expect(n.length, closeTo(1, 1e-9));
        expect(n.x, closeTo(0.6, 1e-6));
        expect(n.y, closeTo(0.8, 1e-6));
      });

      test('zero vector normalized is NaN-safe', () {
        final n = Vec3(0, 0, 0).normalized();
        // Should not throw; may produce NaN/Infinity which is acceptable
        expect(n.x.isNaN || n.x.isInfinite || n.x == 0, isTrue);
      });
    });
  });
}
