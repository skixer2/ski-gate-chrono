import 'package:flutter/material.dart';
import '../../models/course_gate.dart';
import '../../models/gate_side.dart';

enum SetupMode { none, newCourse, updateExisting }

class CourseSetupScreen extends StatefulWidget {
  final Course? existingCourse;
  /// Called when a course is saved (Mode A: new course created; Mode B: existing course updated).
  /// Pass `null` if the user cancelled without saving.
  final void Function(Course)? onSave;

  const CourseSetupScreen({
    super.key,
    this.existingCourse,
    this.onSave,
  });

  @override
  State<CourseSetupScreen> createState() => _CourseSetupScreenState();
}

class _CourseSetupScreenState extends State<CourseSetupScreen> {
  SetupMode _mode = SetupMode.none;
  int _gateCount = 0;
  final List<CourseGate> _gates = [];

  void _startNewCourse() {
    setState(() {
      _mode = SetupMode.newCourse;
      _gateCount = 0;
      _gates.clear();
    });
  }

  void _startUpdateExisting() {
    if (widget.existingCourse == null) return;
    setState(() {
      _mode = SetupMode.updateExisting;
      // Load existing course gates into editing state.
      _gates.clear();
      _gates.addAll(widget.existingCourse!.gates);
      _gateCount = _gates.length;
    });
  }

  void _recordGate() {
    setState(() {
      _gateCount++;
      _gates.add(CourseGate(
        gateNumber: _gateCount,
        side: (_gateCount % 2 == 1) ? GateSide.rightGate : GateSide.leftGate,
        // TODO: deltaP populated from phone barometer reading during setup.
        // TODO: deltaLat/deltaLon populated from phone GPS during setup.
        deltaP: null,
        deltaLat: null,
        deltaLon: null,
      ));
    });
  }

  void _finishCourse() {
    if (_mode == SetupMode.newCourse) {
      final nowMs = DateTime.now().millisecondsSinceEpoch;
      final nowUnix = nowMs ~/ 1000;
      final course = Course(
        id: 'course_$nowMs',
        name: 'Course ${_gates.length} gates',
        createdAtUnix: nowUnix,
        // TODO: pStart should be the barometric pressure at START gate (gate 0),
        // obtained from phone sensors during setup.
        pStart: 0.0,
        gates: List.unmodifiable(_gates),
      );
      widget.onSave?.call(course);
    } else if (_mode == SetupMode.updateExisting) {
      // Update the existing course with the edited gate list.
      if (widget.existingCourse == null) return;
      final updated = Course(
        id: widget.existingCourse!.id,
        name: widget.existingCourse!.name,
        createdAtUnix: widget.existingCourse!.createdAtUnix,
        pStart: widget.existingCourse!.pStart,
        gates: List.unmodifiable(_gates),
      );
      widget.onSave?.call(updated);
    }

    setState(() {
      _mode = SetupMode.none;
    });

    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Course saved with $_gateCount gates')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(_mode == SetupMode.none
            ? 'Course Setup'
            : _mode == SetupMode.newCourse
                ? 'New Course ($_gateCount gates)'
                : 'Update Course ($_gateCount gates)'),
      ),
      body: _mode == SetupMode.none ? _buildModeSelector() : _buildSetupActive(),
      floatingActionButton: _mode == SetupMode.none ? null : _buildFAB(),
    );
  }

  Widget _buildModeSelector() {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Icon(Icons.flag_outlined, size: 64, color: Colors.blue),
          const SizedBox(height: 24),
          const Text('Set up a course by walking the gates',
              style: TextStyle(fontSize: 16)),
          const SizedBox(height: 32),
          FilledButton.icon(
            onPressed: _startNewCourse,
            icon: const Icon(Icons.add),
            label: const Text('Mode A: New Course'),
          ),
          const SizedBox(height: 12),
          OutlinedButton.icon(
            onPressed: widget.existingCourse != null ? _startUpdateExisting : null,
            icon: const Icon(Icons.edit),
            label: const Text('Mode B: Update Existing'),
          ),
        ],
      ),
    );
  }

  Widget _buildSetupActive() {
    return Column(
      children: [
        const SizedBox(height: 24),
        // Gate recording prompt.
        Center(
          child: Column(
            children: [
              Icon(Icons.touch_app, size: 80, color: Colors.blue.shade300),
              const SizedBox(height: 16),
              const Text('Tap to record next gate', style: TextStyle(fontSize: 18)),
              const SizedBox(height: 8),
              Text('Gates recorded: $_gateCount',
                  style: TextStyle(color: Colors.grey.shade600)),
            ],
          ),
        ),
        const SizedBox(height: 16),
        // Scrollable list of recorded gates (editable in both modes).
        if (_gates.isNotEmpty)
          Expanded(
            child: ListView.builder(
              padding: const EdgeInsets.symmetric(horizontal: 16),
              itemCount: _gates.length,
              itemBuilder: (context, index) {
                final gate = _gates[index];
                return ListTile(
                  leading: CircleAvatar(
                    child: Text('${gate.gateNumber}'),
                  ),
                  title: Text('Gate ${gate.gateNumber}'),
                  subtitle: Text('Side: ${gate.side.name}'),
                  trailing: IconButton(
                    icon: const Icon(Icons.delete, color: Colors.red),
                    onPressed: () {
                      setState(() {
                        _gates.removeAt(index);
                        _gateCount = _gates.length;
                      });
                    },
                  ),
                );
              },
            ),
          ),
      ],
    );
  }

  Widget? _buildFAB() {
    return Row(
      mainAxisAlignment: MainAxisAlignment.end,
      children: [
        // Record gate button.
        FloatingActionButton.extended(
          heroTag: 'record',
          onPressed: _recordGate,
          icon: const Icon(Icons.add_location),
          label: Text('Gate ${_gateCount + 1}'),
        ),
        const SizedBox(width: 12),
        // Finish / save course button.
        FloatingActionButton.extended(
          heroTag: 'finish',
          onPressed: _gates.isNotEmpty ? _finishCourse : null,
          icon: const Icon(Icons.check),
          label: const Text('Finish'),
          backgroundColor: Colors.green,
          foregroundColor: Colors.white,
        ),
      ],
    );
  }
}
