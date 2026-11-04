import '../models/run.dart';
import '../models/course_gate.dart';

class LocalDB {
  LocalDB._();
  static final instance = LocalDB._();

  Future<void> init() async {
    // In production: sqflite.openDatabase()
  }

  Future<int> insertRun(Run run) async => run.id;
  Future<List<Run>> getAllRuns() async => [];
  Future<Run?> getRun(int id) async => null;
  Future<void> deleteRun(int id) async {}
  Future<void> insertCourse(Course course) async {}
  Future<Course?> getCourse(String id) async => null;
  Future<List<Course>> getAllCourses() async => [];
}
