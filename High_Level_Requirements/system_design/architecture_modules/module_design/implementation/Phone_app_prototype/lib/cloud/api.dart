import '../models/run.dart';
import '../models/course_gate.dart';

class CloudAPI {
  static final instance = CloudAPI._();
  CloudAPI._();

  Future<bool> uploadRun(Run run) async => false;
  Future<Course?> fetchCourse(String courseId) async => null;
  Future<List<Course>> fetchNearbyCourses(double lat, double lon) async => [];
  bool get isLoggedIn => false;
}
