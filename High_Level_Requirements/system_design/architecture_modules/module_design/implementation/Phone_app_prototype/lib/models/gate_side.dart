enum GateSide { leftGate, rightGate }

enum ArmSide {
  left,
  right;
  String get label => this == ArmSide.left ? 'Left' : 'Right';
}

enum Discipline {
  sl, gs, sg, dh;
  String get label => switch (this) {
        Discipline.sl => 'Slalom',
        Discipline.gs => 'Giant Slalom',
        Discipline.sg => 'Super-G',
        Discipline.dh => 'Downhill',
      };
}
