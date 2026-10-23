import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// F20 (2026-10-23): athlete profile — read/write, persisted locally in
/// SharedPreferences. These fields ride along with cloud upload
/// attribution once the remote-DB arc lands (F23/F25). Spec acceptance:
/// "Enter values, close app, reopen, verify persistence."
class ProfileScreen extends StatefulWidget {
  const ProfileScreen({super.key});
  @override
  State<ProfileScreen> createState() => _ProfileScreenState();
}

class _ProfileScreenState extends State<ProfileScreen> {
  static const _kName = 'sgc.profile.name';
  static const _kClub = 'sgc.profile.club';
  static const _kGroup = 'sgc.profile.group';
  static const _kAge = 'sgc.profile.age';
  static const _kCategory = 'sgc.profile.category';
  static const _kPush = 'sgc.profile.push';

  final _name = TextEditingController();
  final _club = TextEditingController();
  final _group = TextEditingController();
  final _age = TextEditingController();
  final _category = TextEditingController();
  bool _push = false;
  bool _loaded = false;
  String _status = '';

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final p = await SharedPreferences.getInstance();
    if (!mounted) return;
    setState(() {
      _name.text = p.getString(_kName) ?? '';
      _club.text = p.getString(_kClub) ?? '';
      _group.text = p.getString(_kGroup) ?? '';
      _age.text = p.getString(_kAge) ?? '';
      _category.text = p.getString(_kCategory) ?? '';
      _push = p.getBool(_kPush) ?? true; // default on (F23 attribution)
      _loaded = true;
    });
  }

  Future<void> _save() async {
    final p = await SharedPreferences.getInstance();
    await p.setString(_kName, _name.text.trim());
    await p.setString(_kClub, _club.text.trim());
    await p.setString(_kGroup, _group.text.trim());
    await p.setString(_kAge, _age.text.trim());
    await p.setString(_kCategory, _category.text.trim());
    await p.setBool(_kPush, _push);
    if (!mounted) return;
    setState(() => _status = 'Saved ${DateTime.now().toString().substring(11, 19)} UTC');
  }

  @override
  void dispose() {
    _name.dispose();
    _club.dispose();
    _group.dispose();
    _age.dispose();
    _category.dispose();
    super.dispose();
  }

  Widget _field(String label, TextEditingController ctrl,
      {TextInputType keyboard = TextInputType.text,
      List<TextInputFormatter>? inputFormatters,
      String? hint}) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: TextField(
        controller: ctrl,
        keyboardType: keyboard,
        inputFormatters: inputFormatters,
        decoration: InputDecoration(
          labelText: label,
          hintText: hint,
          border: const OutlineInputBorder(),
          isDense: true,
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Athlete Profile')),
      body: !_loaded
          ? const Center(child: CircularProgressIndicator())
          : ListView(
              padding: const EdgeInsets.all(16),
              children: [
                _field('Athlete name', _name, hint: 'e.g. Jean-Paul Voyat'),
                _field('Ski club', _club, hint: 'e.g. Ski Club Les Agettes'),
                _field('Group', _group, hint: 'e.g. Masters / U16 race group'),
                _field('Age', _age,
                    keyboard: TextInputType.number,
                    inputFormatters: [FilteringTextInputFormatter.digitsOnly],
                    hint: 'e.g. 49'),
                _field('Category', _category, hint: 'e.g. Master, U16, FIS…'),
                SwitchListTile(
                  secondary: const Icon(Icons.cloud_upload),
                  title: const Text('Push runs to cloud'),
                  subtitle: const Text(
                      'Attribution uses this profile (remote DB arc — stored for now)'),
                  value: _push,
                  onChanged: (v) => setState(() => _push = v),
                ),
                const SizedBox(height: 8),
                FilledButton.icon(
                  icon: const Icon(Icons.save),
                  label: const Text('Save profile'),
                  onPressed: _save,
                ),
                if (_status.isNotEmpty)
                  Padding(
                    padding: const EdgeInsets.only(top: 8),
                    child: Text(_status,
                        textAlign: TextAlign.center,
                        style: TextStyle(
                            fontSize: 12, color: Colors.grey.shade600)),
                  ),
              ],
            ),
    );
  }
}
