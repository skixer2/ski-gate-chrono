import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:flutter_secure_storage/flutter_secure_storage.dart';

/// Signed-in cloud user as returned by the server (subset of
/// ProfileResponse that the app needs to display). The account NAME is the
/// login identifier in v1 (REQ-DB has no email field) — renaming it would
/// silently change the credential, so the phone never PUTs it.
class CloudUser {
  final int id;
  final String name;
  final String userType; // 'athlete' | 'master' in v1
  final int? age;
  final String? category;

  const CloudUser({
    required this.id,
    required this.name,
    required this.userType,
    this.age,
    this.category,
  });

  Map<String, dynamic> toJson() => {
        'id': id,
        'name': name,
        'user_type': userType,
        if (age != null) 'age': age,
        if (category != null) 'category': category,
      };

  factory CloudUser.fromJson(Map<String, dynamic> json) => CloudUser(
        id: json['id'] as int,
        name: json['name'] as String,
        userType: json['user_type'] as String? ?? 'athlete',
        age: json['age'] as int?,
        category: json['category'] as String?,
      );
}

/// Token pair + user, persisted in flutter_secure_storage (encrypted
/// keystore). Tokens must NOT live in SharedPreferences (plain text on
/// disk) — same discipline as the firmware config in flash.
class AuthSession {
  final String accessToken;
  final String refreshToken;
  final CloudUser user;

  const AuthSession({
    required this.accessToken,
    required this.refreshToken,
    required this.user,
  });
}

/// Persistence for the cloud session.
///
/// Singleton with an in-memory cache so the UI can do a cheap synchronous
/// `session` check after the initial [load]; every mutation goes through
/// [save]/[clear] so the cache and the keystore never diverge.
class AuthStore {
  static final AuthStore instance = AuthStore._();
  AuthStore._();

  static const _kAccess = 'sgc.cloud.access_token';
  static const _kRefresh = 'sgc.cloud.refresh_token';
  static const _kUser = 'sgc.cloud.user';

  final FlutterSecureStorage _storage = const FlutterSecureStorage();

  AuthSession? _cached;

  /// Currently cached session (set after the first [load]); null = unknown
  /// or signed out. UI should prefer the async [load] on screen entry.
  AuthSession? get session => _cached;

  Future<bool> isSignedIn() async => await load() != null;

  /// Load the session from the keystore (cached after the first read).
  Future<AuthSession?> load() async {
    if (_cached != null) return _cached;
    try {
      final access = await _storage.read(key: _kAccess);
      final refresh = await _storage.read(key: _kRefresh);
      final userJson = await _storage.read(key: _kUser);
      if (access == null || refresh == null || userJson == null) return null;
      _cached = AuthSession(
        accessToken: access,
        refreshToken: refresh,
        user: CloudUser.fromJson(jsonDecode(userJson) as Map<String, dynamic>),
      );
      return _cached;
    } catch (e) {
      debugPrint('[AuthStore] load failed: $e');
      return null;
    }
  }

  /// Persist a fresh session (login, register+login, refresh rotation).
  Future<void> save(AuthSession session) async {
    _cached = session;
    await _storage.write(key: _kAccess, value: session.accessToken);
    await _storage.write(key: _kRefresh, value: session.refreshToken);
    await _storage.write(
        key: _kUser, value: jsonEncode(session.user.toJson()));
  }

  /// Update only the cached user row (e.g. after a profile PUT echo).
  Future<void> updateUser(CloudUser user) async {
    final s = _cached;
    if (s == null) return;
    await save(AuthSession(
      accessToken: s.accessToken,
      refreshToken: s.refreshToken,
      user: user,
    ));
  }

  /// Sign out (or post-GDPR cleanup): wipe tokens from the keystore.
  Future<void> clear() async {
    _cached = null;
    await _storage.delete(key: _kAccess);
    await _storage.delete(key: _kRefresh);
    await _storage.delete(key: _kUser);
  }
}
