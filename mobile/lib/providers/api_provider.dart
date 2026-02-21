import 'dart:convert';
import 'dart:io';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:flutter_secure_storage/flutter_secure_storage.dart';

import '../models/robot_status.dart';
import '../models/personality_config.dart';

class ApiProvider extends ChangeNotifier {
  static const String _tokenKey = 'minbot_jwt_token';

  String _baseUrl = 'http://192.168.1.100:8000';
  String? _token;
  bool _isLoading = false;
  String _errorMessage = '';

  final FlutterSecureStorage _storage = const FlutterSecureStorage();

  String get baseUrl => _baseUrl;
  bool get isLoading => _isLoading;
  String get errorMessage => _errorMessage;
  bool get isAuthenticated => _token != null && _token!.isNotEmpty;

  ApiProvider() {
    _loadToken();
  }

  Future<void> _loadToken() async {
    _token = await _storage.read(key: _tokenKey);
    notifyListeners();
  }

  void updateBaseUrl(String url) {
    if (url.isNotEmpty && url != _baseUrl) {
      _baseUrl = url;
      notifyListeners();
    }
  }

  Map<String, String> get _headers {
    final headers = <String, String>{
      'Content-Type': 'application/json',
    };
    if (_token != null && _token!.isNotEmpty) {
      headers['Authorization'] = 'Bearer $_token';
    }
    return headers;
  }

  Future<bool> login(String password) async {
    _setLoading(true);
    try {
      final response = await http
          .post(
            Uri.parse('$_baseUrl/api/auth/login'),
            headers: {'Content-Type': 'application/json'},
            body: jsonEncode({'password': password}),
          )
          .timeout(const Duration(seconds: 10));

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body) as Map<String, dynamic>;
        _token = data['access_token'] as String?;
        if (_token != null) {
          await _storage.write(key: _tokenKey, value: _token);
        }
        _setLoading(false);
        return true;
      } else {
        _errorMessage = '인증 실패: 비밀번호를 확인해주세요.';
        _setLoading(false);
        return false;
      }
    } catch (e) {
      _errorMessage = '서버 연결 실패: $e';
      _setLoading(false);
      return false;
    }
  }

  Future<void> logout() async {
    _token = null;
    await _storage.delete(key: _tokenKey);
    notifyListeners();
  }

  Future<RobotStatus?> getStatus() async {
    try {
      final response = await http
          .get(Uri.parse('$_baseUrl/api/status'), headers: _headers)
          .timeout(const Duration(seconds: 5));

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body) as Map<String, dynamic>;
        return RobotStatus.fromJson(data);
      }
      return null;
    } catch (_) {
      return null;
    }
  }

  Future<bool> uploadVoiceSamples(List<File> files) async {
    _setLoading(true);
    try {
      final request = http.MultipartRequest(
        'POST',
        Uri.parse('$_baseUrl/api/voice-samples'),
      );
      if (_token != null) {
        request.headers['Authorization'] = 'Bearer $_token';
      }
      for (final file in files) {
        request.files.add(await http.MultipartFile.fromPath(
          'samples',
          file.path,
        ));
      }
      final streamedResponse =
          await request.send().timeout(const Duration(seconds: 60));
      final success = streamedResponse.statusCode == 200 ||
          streamedResponse.statusCode == 201;
      if (!success) _errorMessage = '음성 샘플 업로드에 실패했습니다.';
      _setLoading(false);
      return success;
    } catch (e) {
      _errorMessage = '업로드 오류: $e';
      _setLoading(false);
      return false;
    }
  }

  Future<PersonalityConfig?> getPersonality() async {
    try {
      final response = await http
          .get(Uri.parse('$_baseUrl/api/personality'), headers: _headers)
          .timeout(const Duration(seconds: 10));

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body) as Map<String, dynamic>;
        return PersonalityConfig.fromJson(data);
      }
      return null;
    } catch (_) {
      return null;
    }
  }

  Future<bool> updatePersonality(PersonalityConfig config) async {
    _setLoading(true);
    try {
      final response = await http
          .put(
            Uri.parse('$_baseUrl/api/personality'),
            headers: _headers,
            body: jsonEncode(config.toJson()),
          )
          .timeout(const Duration(seconds: 10));
      _setLoading(false);
      return response.statusCode == 200;
    } catch (e) {
      _errorMessage = '설정 저장 실패: $e';
      _setLoading(false);
      return false;
    }
  }

  Future<bool> changeVoiceProvider(String provider) async {
    _setLoading(true);
    try {
      final response = await http
          .post(
            Uri.parse('$_baseUrl/api/voice-provider'),
            headers: _headers,
            body: jsonEncode({'provider': provider}),
          )
          .timeout(const Duration(seconds: 10));
      _setLoading(false);
      return response.statusCode == 200;
    } catch (e) {
      _errorMessage = '음성 제공자 변경 실패: $e';
      _setLoading(false);
      return false;
    }
  }

  Future<List<Map<String, dynamic>>> getConversations({int limit = 20}) async {
    try {
      final response = await http
          .get(
            Uri.parse('$_baseUrl/api/conversations?limit=$limit'),
            headers: _headers,
          )
          .timeout(const Duration(seconds: 10));

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body);
        if (data is List) {
          return data.cast<Map<String, dynamic>>();
        }
      }
      return [];
    } catch (_) {
      return [];
    }
  }

  Future<bool> triggerOtaUpdate() async {
    _setLoading(true);
    try {
      final response = await http
          .post(
            Uri.parse('$_baseUrl/api/ota/update'),
            headers: _headers,
          )
          .timeout(const Duration(seconds: 10));
      _setLoading(false);
      return response.statusCode == 200 || response.statusCode == 202;
    } catch (e) {
      _errorMessage = 'OTA 업데이트 요청 실패: $e';
      _setLoading(false);
      return false;
    }
  }

  void _setLoading(bool value) {
    _isLoading = value;
    if (value) _errorMessage = '';
    notifyListeners();
  }
}
