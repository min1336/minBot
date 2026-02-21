import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

class SettingsProvider extends ChangeNotifier {
  static const String _keyServerUrl = 'server_url';
  static const String _keyWakeWord = 'wake_word';
  static const String _keyEmotionSensitivity = 'emotion_sensitivity';
  static const String _keyDarkMode = 'dark_mode';

  SharedPreferences? _prefs;

  String _serverUrl = 'http://192.168.1.100:8000';
  String _wakeWord = '민봇아';
  double _emotionSensitivity = 0.5;
  bool _darkMode = false;

  String get serverUrl => _serverUrl;
  String get wakeWord => _wakeWord;
  double get emotionSensitivity => _emotionSensitivity;
  bool get darkMode => _darkMode;

  Future<void> init() async {
    _prefs = await SharedPreferences.getInstance();
    _serverUrl = _prefs?.getString(_keyServerUrl) ?? _serverUrl;
    _wakeWord = _prefs?.getString(_keyWakeWord) ?? _wakeWord;
    _emotionSensitivity =
        _prefs?.getDouble(_keyEmotionSensitivity) ?? _emotionSensitivity;
    _darkMode = _prefs?.getBool(_keyDarkMode) ?? _darkMode;
    notifyListeners();
  }

  Future<void> setServerUrl(String value) async {
    _serverUrl = value;
    await _prefs?.setString(_keyServerUrl, value);
    notifyListeners();
  }

  Future<void> setWakeWord(String value) async {
    _wakeWord = value;
    await _prefs?.setString(_keyWakeWord, value);
    notifyListeners();
  }

  Future<void> setEmotionSensitivity(double value) async {
    _emotionSensitivity = value;
    await _prefs?.setDouble(_keyEmotionSensitivity, value);
    notifyListeners();
  }

  Future<void> setDarkMode(bool value) async {
    _darkMode = value;
    await _prefs?.setBool(_keyDarkMode, value);
    notifyListeners();
  }
}
