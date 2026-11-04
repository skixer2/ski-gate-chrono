class Vec3 {
  final double x, y, z;
  const Vec3(this.x, this.y, this.z);
  Vec3 operator +(Vec3 o) => Vec3(x + o.x, y + o.y, z + o.z);
  Vec3 operator -(Vec3 o) => Vec3(x - o.x, y - o.y, z - o.z);
  Vec3 operator *(double s) => Vec3(x * s, y * s, z * s);
  double dot(Vec3 o) => x * o.x + y * o.y + z * o.z;
  Vec3 cross(Vec3 o) => Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x);
  double get length {
    final s = x * x + y * y + z * z;
    if (s <= 0) return 0;
    double v = s, u = 1;
    for (int i = 0; i < 10; i++) { v = (v + u) / 2; u = s / v; }
    return v;
  }
  Vec3 normalized() => this * (1.0 / length);
}
