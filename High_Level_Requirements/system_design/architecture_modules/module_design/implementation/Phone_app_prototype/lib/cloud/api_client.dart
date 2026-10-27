import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';

import 'auth_store.dart';

/// I07 bootstrap indirection: the app hardcodes THIS address; the payload
/// tells it where the real API lives (survives host/domain moves without
/// an app release). All /v1 traffic goes to the (cached) api_base.
const String kCloudBootstrapUrl = 'https://sgc-api.vytsolutions.com';

// ── Typed exceptions ────────────────────────────────────────────────────
// The sync worker branches on these: 413/validation → POISONED (never
// retried silently), auth → refresh-once then give up, 403 → backoff,
// 5xx/network → backoff retry.

/// Base class: carries the HTTP status when the error came from a response.
class CloudException implements Exception {
  final int? statusCode;
  final String message;
  const CloudException(this.message, {this.statusCode});

  /// Server error payloads can be plain strings or {code, detail} objects
  /// (FastAPI HTTPException with a dict detail) — surface whichever exists.
  static String extractDetail(dynamic decoded, String body) {
    if (decoded is Map<String, dynamic>) {
      final detail = decoded['detail'];
      if (detail is String) return detail;
      if (detail is Map<String, dynamic>) {
        final code = detail['code'];
        final d = detail['detail'];
        if (code != null && d != null) return '$code: $d';
        return d?.toString() ?? detail.toString();
      }
      if (detail != null) return detail.toString();
    }
    return body.isEmpty ? 'unknown error' : body;
  }

  @override
  String toString() => message;
}

/// 401 after a refresh attempt failed — session is dead, sign in again.
class AuthException extends CloudException {
  const AuthException(super.message, {super.statusCode});
}

/// 403: activation window expired / club license expired — writes blocked,
/// reads still work (§5). Retry later, never poison.
class ActivationException extends CloudException {
  const ActivationException(super.message, {super.statusCode});
}

/// 409: conflict (lock_version mismatch, duplicate name on register).
class ConflictException extends CloudException {
  const ConflictException(super.message, {super.statusCode});
}

/// 413: payload over server row caps — mark POISONED, never retry.
class TooLargeException extends CloudException {
  const TooLargeException(super.message, {super.statusCode});
}

/// 400/422: the server rejected the payload as invalid — a retry with the
/// SAME bytes will fail identically, so callers mark POISONED.
class ValidationException extends CloudException {
  const ValidationException(super.message, {super.statusCode});
}

/// 5xx: server-side problem — retryable with backoff.
class ServerException extends CloudException {
  const ServerException(super.message, {super.statusCode});
}

/// No connectivity / timeout / DNS — retryable with backoff.
class NetworkException extends CloudException {
  const NetworkException(super.message);
}

/// Token pair returned by register/login/refresh (register returns only the
/// user row in the deployed v1 API — the app logs in right after).
class TokenPair {
  final String accessToken;
  final String refreshToken;
  const TokenPair({required this.accessToken, required this.refreshToken});

  factory TokenPair.fromJson(Map<String, dynamic> json) => TokenPair(
        accessToken: json['access_token'] as String,
        refreshToken: json['refresh_token'] as String,
      );
}

/// SGC cloud REST client (P1).
///
/// - Bearer token attach + ONE automatic retry through /v1/auth/refresh on
///   401, behind a mutex so N concurrent 401s cause exactly one refresh
///   (refresh tokens are single-use / rotated server-side — a storm would
///   brick the session).
/// - Bootstrap: cached last-known-good api_base in SharedPreferences;
///   /bootstrap-info refreshes it in the background at app start.
/// - Everything else maps status codes onto the typed exceptions above.
class ApiClient {
  static final ApiClient instance = ApiClient._();
  ApiClient._();

  static const _kApiBase = 'sgc.cloud.api_base';
  static const _kMinAppVersion = 'sgc.cloud.min_app_version';

  final AuthStore _auth = AuthStore.instance;
  http.Client _http = http.Client();

  String _apiBase = kCloudBootstrapUrl;
  String? _minAppVersion;
  bool _initialized = false;
  final Completer<void> _ready = Completer<void>();

  String get apiBase => _apiBase;

  /// Last-known min_app_version from bootstrap (null = never fetched).
  String? get minAppVersion => _minAppVersion;

  /// Completes once the cached api_base is loaded — network passes in the
  /// sync worker await this so they never hit the hardcoded placeholder.
  Future<void> get ready => _ready.future;

  /// Call once at app start (main.dart). Loads the cached base URL, then
  /// refreshes bootstrap-info in the background (non-fatal: offline start
  /// keeps the last-known-good base).
  Future<void> init() async {
    if (_initialized) return;
    _initialized = true;
    try {
      final prefs = await SharedPreferences.getInstance();
      _apiBase = prefs.getString(_kApiBase) ?? kCloudBootstrapUrl;
      _minAppVersion = prefs.getString(_kMinAppVersion);
    } catch (e) {
      debugPrint('[ApiClient] prefs load failed: $e');
    }
    if (!_ready.isCompleted) _ready.complete();
    _refreshBootstrap();
  }

  Future<void> _refreshBootstrap() async {
    try {
      final resp = await _http
          .get(Uri.parse('$kCloudBootstrapUrl/bootstrap-info'))
          .timeout(const Duration(seconds: 15));
      if (resp.statusCode != 200) return;
      final map = jsonDecode(resp.body) as Map<String, dynamic>;
      final base = map['api_base'] as String?;
      final minV = map['min_app_version'] as String?;
      if (base != null && base.isNotEmpty) {
        _apiBase = base;
        final prefs = await SharedPreferences.getInstance();
        await prefs.setString(_kApiBase, base);
      }
      if (minV != null && minV.isNotEmpty) {
        _minAppVersion = minV;
        final prefs = await SharedPreferences.getInstance();
        await prefs.setString(_kMinAppVersion, minV);
      }
      debugPrint('[ApiClient] bootstrap: base=$_apiBase min=$_minAppVersion');
    } catch (e) {
      // Offline / bootstrap host down: keep last-known-good base silently.
      debugPrint('[ApiClient] bootstrap refresh skipped: $e');
    }
  }

  // ── Auth ──────────────────────────────────────────────────────────────

  /// POST /v1/auth/register — v1 self-registration athlete/master only.
  /// The deployed v1 endpoint returns 201 + {id, name, user_type} (NO
  /// tokens), so callers follow up with [login].
  Future<Map<String, dynamic>> register({
    required String name,
    required String password,
    required String userType, // 'athlete' | 'master'
    int? age,
    String? category,
  }) async {
    return _request('POST', '/v1/auth/register',
        authed: false,
        jsonBody: {
          'name': name,
          'password': password,
          'user_type': userType,
          if (age != null) 'age': age,
          if (category != null && category.isNotEmpty) 'category': category,
        },
        expectStatus: 201);
  }

  /// POST /v1/auth/login → token pair; persists the session.
  Future<CloudUser> login({
    required String name,
    required String password,
  }) async {
    final body = await _request('POST', '/v1/auth/login',
        authed: false, jsonBody: {'name': name, 'password': password});
    await _storeTokens(body);
    final session = await _auth.load();
    return session!.user;
  }

  /// POST /v1/auth/refresh with the stored refresh token. Single retry
  /// path for 401s. On failure the local session is wiped (the refresh
  /// token is single-use and rotated server-side — a dead one is dead).
  /// Mutex'd: concurrent callers share one in-flight refresh.
  Future<bool>? _refreshing;
  Future<bool> refreshTokens() {
    final inFlight = _refreshing;
    if (inFlight != null) return inFlight;
    final op = _doRefresh();
    _refreshing = op;
    // ignore: unawaited_futures
    op.whenComplete(() => _refreshing = null);
    return op;
  }

  Future<bool> _doRefresh() async {
    final session = await _auth.load();
    if (session == null) return false;
    try {
      final body = await _request('POST', '/v1/auth/refresh',
          authed: false,
          jsonBody: {'refresh_token': session.refreshToken});
      await _storeTokens(body);
      debugPrint('[ApiClient] token refresh OK');
      return true;
    } on CloudException catch (e) {
      debugPrint('[ApiClient] refresh failed (${e.statusCode}): $e — signing out');
      await _auth.clear();
      return false;
    }
  }

  Future<void> _storeTokens(Map<String, dynamic> body) async {
    final pair = TokenPair.fromJson(body);
    final old = await _auth.load();
    final user = old?.user;
    if (user == null) {
      // Login path without a known user row: /v1/profile GET would give the
      // full row, but login only happens right after the user typed their
      // name — fetch the profile to fill the cache.
      final profile = await _getProfileRaw(pair.accessToken);
      final newUser = CloudUser(
        id: profile['id'] as int,
        name: profile['name'] as String,
        userType: profile['user_type'] as String? ?? 'athlete',
        age: profile['age'] as int?,
        category: profile['category'] as String?,
      );
      await _auth.save(AuthSession(
          accessToken: pair.accessToken,
          refreshToken: pair.refreshToken,
          user: newUser));
      return;
    }
    await _auth.save(AuthSession(
        accessToken: pair.accessToken,
        refreshToken: pair.refreshToken,
        user: user));
  }

  // ── Profile (F20 mirror) ──────────────────────────────────────────────

  Future<Map<String, dynamic>> _getProfileRaw(String accessToken) async {
    return await _request('GET', '/v1/profile',
        accessTokenOverride: accessToken);
  }

  /// GET /v1/profile.
  Future<Map<String, dynamic>> getProfile() =>
      _request('GET', '/v1/profile');

  /// PUT /v1/profile — only the fields the server schema accepts
  /// (name/age/category). The phone deliberately never sends `name`:
  /// users.name IS the login credential in v1, and PUT would rename the
  /// account (subsequent logins would then fail with the old name).
  Future<Map<String, dynamic>> updateProfile({int? age, String? category}) {
    return _request('PUT', '/v1/profile', jsonBody: {
      if (age != null) 'age': age,
      if (category != null && category.isNotEmpty) 'category': category,
    });
  }

  // ── Runs (F23 upload, F48 browse) ─────────────────────────────────────

  /// POST /v1/runs — one transaction server-side, idempotent on
  /// client_run_uuid (retry → 200 already_exists=true). Returns
  /// {id, lock_version, already_exists}.
  Future<Map<String, dynamic>> uploadRun(Map<String, dynamic> payload) async {
    return await _request('POST', '/v1/runs', jsonBody: payload);
  }

  /// GET /v1/runs?limit&cursor — own runs, keyset-paged.
  /// Returns {'items': [...], 'nextCursor': String?}.
  Future<Map<String, dynamic>> listRuns({int limit = 50, String? cursor}) async {
    final body = await _request(
        'GET',
        '/v1/runs?limit=$limit'
        '${cursor != null ? '&cursor=$cursor' : ''}');
    return {
      'items': (body['items'] as List<dynamic>? ?? [])
          .cast<Map<String, dynamic>>(),
      'nextCursor': body['next_cursor'] as String?,
    };
  }

  /// GET /v1/runs/{id}?include=timestamps,barometric_data.
  Future<Map<String, dynamic>> getRun(int runId,
      {String include = 'timestamps,barometric_data'}) async {
    return await _request('GET', '/v1/runs/$runId?include=$include');
  }

  /// DELETE /v1/runs/{id} → 204 (never activation-blocked server-side).
  Future<void> deleteRun(int runId) async {
    await _request('DELETE', '/v1/runs/$runId', expectStatus: 204);
  }

  /// PUT /v1/runs/{id} — rename (F45 propagation, 1.70). Server contract
  /// (sgc_api app/routers/runs.py): rename is a WRITE gated by optimistic
  /// concurrency — `If-Match: <lock_version>` header REQUIRED (missing ⇒
  /// 428, mismatch ⇒ 409, hit ⇒ lock_version += 1). Returns the updated
  /// RunOut (fresh lock_version). Body: {"name": "..."} (1..200 chars).
  Future<Map<String, dynamic>> renameRun(
    int runId, {
    required String name,
    required int ifMatch,
  }) async {
    return _request('PUT', '/v1/runs/$runId',
        jsonBody: {'name': name},
        extraHeaders: {'If-Match': '$ifMatch'});
  }

  // ── GDPR (F50) ────────────────────────────────────────────────────────

  /// POST /v1/me/gdpr-delete {confirm: true} → 204. Hard-deletes the user
  /// row + every run/timestamp/barometric row + all refresh tokens.
  /// Everything 404s afterwards; re-registration with the same name is
  /// possible. Caller clears local session + upload queue after success.
  Future<void> gdprDelete() async {
    await _request('POST', '/v1/me/gdpr-delete',
        jsonBody: {'confirm': true}, expectStatus: 204);
  }

  // ── Plumbing ──────────────────────────────────────────────────────────

  /// Single request with bearer attach, typed error mapping and the
  /// one-shot 401→refresh→retry. Returns the decoded JSON body (map) or,
  /// for [expectStatus] 204, an empty map.
  Future<Map<String, dynamic>> _request(
    String method,
    String path, {
    Object? jsonBody,
    bool authed = true,
    String? accessTokenOverride,
    int? expectStatus,
    Map<String, String> extraHeaders = const {},
  }) async {
    await ready;

    String? token;
    if (authed) {
      if (accessTokenOverride != null) {
        token = accessTokenOverride;
      } else {
        final session = await _auth.load();
        token = session?.accessToken;
        if (token == null) {
          throw const AuthException('Not signed in', statusCode: 401);
        }
      }
    }

    try {
      return await _attempt(method, path, jsonBody, token, expectStatus,
          extraHeaders);
    } on AuthException {
      // One automatic retry through /v1/auth/refresh. Only for the normal
      // authed path — never for login/register/refresh themselves, and
      // never when the caller pinned a specific token.
      if (!authed || accessTokenOverride != null) rethrow;
      final refreshed = await refreshTokens();
      if (!refreshed) {
        throw const AuthException('Session expired — sign in again',
            statusCode: 401);
      }
      final session = await _auth.load();
      return await _attempt(
          method, path, jsonBody, session?.accessToken, expectStatus,
          extraHeaders);
    }
  }

  Future<Map<String, dynamic>> _attempt(
    String method,
    String path,
    Object? jsonBody,
    String? token,
    int? expectStatus,
    Map<String, String> extraHeaders,
  ) async {
    final uri = Uri.parse('$_apiBase$path');
    final headers = <String, String>{
      'Accept': 'application/json',
      if (jsonBody != null) 'Content-Type': 'application/json',
      if (token != null) 'Authorization': 'Bearer $token',
      ...extraHeaders,
    };
    http.Response resp;
    try {
      final req = http.Request(method, uri)..headers.addAll(headers);
      if (jsonBody != null) req.body = jsonEncode(jsonBody);
      resp = await http.Response.fromStream(await _http.send(req))
          .timeout(const Duration(seconds: 30));
    } on SocketException catch (e) {
      throw NetworkException('No connection: ${e.message}');
    } on HttpException catch (e) {
      throw NetworkException('Network error: ${e.message}');
    } on TimeoutException {
      throw NetworkException('Request timed out');
    } catch (e) {
      // http ClientException and friends — transport-level, retryable.
      throw NetworkException('Network error: $e');
    }

    if (resp.statusCode >= 400) {
      final decoded = _tryDecode(resp.body);
      final msg = CloudException.extractDetail(decoded, resp.body);
      _throwForStatus(resp.statusCode, msg);
    }
    if (expectStatus != null && resp.statusCode != expectStatus) {
      throw CloudException(
          'Unexpected status ${resp.statusCode} (expected $expectStatus)',
          statusCode: resp.statusCode);
    }
    final decoded = _tryDecode(resp.body);
    return decoded is Map<String, dynamic> ? decoded : <String, dynamic>{};
  }

  void _throwForStatus(int status, String msg) {
    switch (status) {
      case 401:
        throw AuthException(msg, statusCode: 401);
      case 403:
        throw ActivationException(msg, statusCode: 403);
      case 409:
        throw ConflictException(msg, statusCode: 409);
      case 413:
        throw TooLargeException(msg, statusCode: 413);
      case 400:
      case 422:
        throw ValidationException(msg, statusCode: status);
      default:
        if (status >= 500) throw ServerException(msg, statusCode: status);
        throw CloudException(msg, statusCode: status);
    }
  }

  dynamic _tryDecode(String body) {
    if (body.isEmpty) return null;
    try {
      return jsonDecode(body);
    } catch (_) {
      return null;
    }
  }
}
