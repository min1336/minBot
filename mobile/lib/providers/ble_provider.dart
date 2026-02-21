import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class BleProvider extends ChangeNotifier {
  static const String _serviceUuid = '12345678-1234-1234-1234-123456789abc';
  static const String _wifiCharUuid = '12345678-1234-1234-1234-123456789abd';
  static const String _statusCharUuid = '12345678-1234-1234-1234-123456789abe';
  static const String _deviceNamePrefix = 'minBot';

  BluetoothDevice? _connectedDevice;
  BluetoothCharacteristic? _wifiCharacteristic;
  BluetoothCharacteristic? _statusCharacteristic;

  List<ScanResult> _scanResults = [];
  bool _isScanning = false;
  bool _isConnecting = false;
  bool _isConnected = false;
  int _batteryLevel = 0;
  String _deviceName = '';
  String _errorMessage = '';

  StreamSubscription<List<ScanResult>>? _scanSubscription;
  StreamSubscription<BluetoothConnectionState>? _connectionSubscription;

  List<ScanResult> get scanResults => _scanResults;
  bool get isScanning => _isScanning;
  bool get isConnecting => _isConnecting;
  bool get isConnected => _isConnected;
  int get batteryLevel => _batteryLevel;
  String get deviceName => _deviceName;
  String get errorMessage => _errorMessage;

  Future<void> scanForDevices() async {
    if (_isScanning) return;

    _scanResults = [];
    _errorMessage = '';
    _isScanning = true;
    notifyListeners();

    try {
      await FlutterBluePlus.startScan(
        timeout: const Duration(seconds: 10),
        withNames: [],
      );

      _scanSubscription = FlutterBluePlus.scanResults.listen((results) {
        _scanResults = results
            .where((r) =>
                r.device.platformName.startsWith(_deviceNamePrefix) ||
                r.advertisementData.localName.startsWith(_deviceNamePrefix))
            .toList();
        notifyListeners();
      });

      await Future.delayed(const Duration(seconds: 10));
    } catch (e) {
      _errorMessage = '스캔 중 오류가 발생했습니다: $e';
    } finally {
      await FlutterBluePlus.stopScan();
      _isScanning = false;
      notifyListeners();
    }
  }

  Future<bool> connect(BluetoothDevice device) async {
    if (_isConnecting) return false;

    _isConnecting = true;
    _errorMessage = '';
    notifyListeners();

    try {
      await device.connect(timeout: const Duration(seconds: 15));

      _connectionSubscription =
          device.connectionState.listen((state) async {
        _isConnected = state == BluetoothConnectionState.connected;
        if (!_isConnected) {
          _batteryLevel = 0;
          _deviceName = '';
          _wifiCharacteristic = null;
          _statusCharacteristic = null;
          _connectedDevice = null;
        }
        notifyListeners();
      });

      final services = await device.discoverServices();
      for (final service in services) {
        if (service.uuid.toString().toLowerCase() ==
            _serviceUuid.toLowerCase()) {
          for (final char in service.characteristics) {
            final uuid = char.uuid.toString().toLowerCase();
            if (uuid == _wifiCharUuid.toLowerCase()) {
              _wifiCharacteristic = char;
            } else if (uuid == _statusCharUuid.toLowerCase()) {
              _statusCharacteristic = char;
              await _subscribeToStatus(char);
            }
          }
        }
      }

      _connectedDevice = device;
      _deviceName = device.platformName.isNotEmpty
          ? device.platformName
          : device.remoteId.str;
      _isConnected = true;
      _isConnecting = false;
      notifyListeners();
      return true;
    } catch (e) {
      _errorMessage = '연결 중 오류가 발생했습니다: $e';
      _isConnecting = false;
      _isConnected = false;
      notifyListeners();
      return false;
    }
  }

  Future<void> _subscribeToStatus(BluetoothCharacteristic char) async {
    try {
      await char.setNotifyValue(true);
      char.lastValueStream.listen((value) {
        if (value.isNotEmpty) {
          try {
            final json =
                jsonDecode(String.fromCharCodes(value)) as Map<String, dynamic>;
            _batteryLevel = (json['battery'] as num?)?.toInt() ?? _batteryLevel;
            notifyListeners();
          } catch (_) {}
        }
      });
    } catch (_) {}
  }

  Future<bool> sendWifiCredentials(String ssid, String password) async {
    if (_wifiCharacteristic == null) {
      _errorMessage = 'Wi-Fi 특성을 찾을 수 없습니다.';
      notifyListeners();
      return false;
    }

    try {
      final payload = jsonEncode({'ssid': ssid, 'password': password});
      await _wifiCharacteristic!.write(
        utf8.encode(payload),
        withoutResponse: false,
      );
      return true;
    } catch (e) {
      _errorMessage = 'Wi-Fi 정보 전송 실패: $e';
      notifyListeners();
      return false;
    }
  }

  Future<void> disconnect() async {
    try {
      await _connectedDevice?.disconnect();
    } catch (_) {}
    _isConnected = false;
    _connectedDevice = null;
    _wifiCharacteristic = null;
    _statusCharacteristic = null;
    _batteryLevel = 0;
    _deviceName = '';
    notifyListeners();
  }

  @override
  void dispose() {
    _scanSubscription?.cancel();
    _connectionSubscription?.cancel();
    super.dispose();
  }
}
