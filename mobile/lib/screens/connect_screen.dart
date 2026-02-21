import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:provider/provider.dart';

import '../providers/ble_provider.dart';

class ConnectScreen extends StatefulWidget {
  const ConnectScreen({super.key});

  @override
  State<ConnectScreen> createState() => _ConnectScreenState();
}

class _ConnectScreenState extends State<ConnectScreen> {
  final _ssidController = TextEditingController();
  final _passwordController = TextEditingController();
  bool _showWifiForm = false;
  bool _passwordVisible = false;
  bool _isSendingWifi = false;

  @override
  void dispose() {
    _ssidController.dispose();
    _passwordController.dispose();
    super.dispose();
  }

  Future<void> _onDeviceTap(BluetoothDevice device) async {
    final ble = context.read<BleProvider>();
    final success = await ble.connect(device);
    if (success && mounted) {
      setState(() => _showWifiForm = true);
    }
  }

  Future<void> _sendWifiCredentials() async {
    final ssid = _ssidController.text.trim();
    final password = _passwordController.text;
    if (ssid.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Wi-Fi 이름(SSID)을 입력해주세요.')),
      );
      return;
    }
    setState(() => _isSendingWifi = true);
    final ble = context.read<BleProvider>();
    final success = await ble.sendWifiCredentials(ssid, password);
    if (mounted) {
      setState(() => _isSendingWifi = false);
      if (success) {
        Navigator.pushReplacementNamed(context, '/dashboard');
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text(ble.errorMessage.isNotEmpty
              ? ble.errorMessage
              : 'Wi-Fi 정보 전송에 실패했습니다.')),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleProvider>();
    final colorScheme = Theme.of(context).colorScheme;

    return Scaffold(
      appBar: AppBar(
        title: const Text('minBot 연결'),
        centerTitle: true,
        backgroundColor: colorScheme.primaryContainer,
      ),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: ble.isConnected && _showWifiForm
              ? _buildWifiForm(colorScheme)
              : _buildScanSection(ble, colorScheme),
        ),
      ),
    );
  }

  Widget _buildScanSection(BleProvider ble, ColorScheme colorScheme) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Card(
          color: colorScheme.primaryContainer,
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: Column(
              children: [
                Icon(Icons.bluetooth_searching,
                    size: 48, color: colorScheme.primary),
                const SizedBox(height: 12),
                Text(
                  '주변의 minBot을 찾습니다',
                  style: Theme.of(context).textTheme.titleMedium?.copyWith(
                        color: colorScheme.onPrimaryContainer,
                      ),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 20),
        FilledButton.icon(
          onPressed: ble.isScanning || ble.isConnecting
              ? null
              : () => context.read<BleProvider>().scanForDevices(),
          icon: ble.isScanning
              ? const SizedBox(
                  width: 18,
                  height: 18,
                  child: CircularProgressIndicator(strokeWidth: 2),
                )
              : const Icon(Icons.search),
          label: Text(ble.isScanning ? '스캔 중...' : '기기 검색'),
        ),
        const SizedBox(height: 16),
        if (ble.errorMessage.isNotEmpty)
          Padding(
            padding: const EdgeInsets.only(bottom: 12),
            child: Text(
              ble.errorMessage,
              style: TextStyle(color: colorScheme.error),
              textAlign: TextAlign.center,
            ),
          ),
        Expanded(
          child: ble.scanResults.isEmpty
              ? Center(
                  child: Text(
                    ble.isScanning
                        ? '검색 중입니다...'
                        : '\'검색\' 버튼을 눌러 minBot을 찾아보세요.',
                    style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                          color: colorScheme.onSurfaceVariant,
                        ),
                    textAlign: TextAlign.center,
                  ),
                )
              : ListView.separated(
                  itemCount: ble.scanResults.length,
                  separatorBuilder: (_, __) => const Divider(height: 1),
                  itemBuilder: (context, i) {
                    final result = ble.scanResults[i];
                    final name = result.device.platformName.isNotEmpty
                        ? result.device.platformName
                        : result.advertisementData.localName.isNotEmpty
                            ? result.advertisementData.localName
                            : result.device.remoteId.str;
                    return ListTile(
                      leading:
                          Icon(Icons.smart_toy, color: colorScheme.primary),
                      title: Text(name),
                      subtitle: Text(result.device.remoteId.str,
                          style: const TextStyle(fontSize: 12)),
                      trailing: ble.isConnecting
                          ? const SizedBox(
                              width: 20,
                              height: 20,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            )
                          : Icon(Icons.chevron_right,
                              color: colorScheme.primary),
                      onTap: ble.isConnecting
                          ? null
                          : () => _onDeviceTap(result.device),
                    );
                  },
                ),
        ),
      ],
    );
  }

  Widget _buildWifiForm(ColorScheme colorScheme) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Card(
          color: colorScheme.secondaryContainer,
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: Row(
              children: [
                Icon(Icons.check_circle, color: colorScheme.secondary),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    '${context.read<BleProvider>().deviceName} 연결됨!\nWi-Fi 정보를 입력해주세요.',
                    style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                          color: colorScheme.onSecondaryContainer,
                        ),
                  ),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 24),
        TextField(
          controller: _ssidController,
          decoration: const InputDecoration(
            labelText: 'Wi-Fi 이름 (SSID)',
            prefixIcon: Icon(Icons.wifi),
            border: OutlineInputBorder(),
          ),
        ),
        const SizedBox(height: 16),
        TextField(
          controller: _passwordController,
          obscureText: !_passwordVisible,
          decoration: InputDecoration(
            labelText: 'Wi-Fi 비밀번호',
            prefixIcon: const Icon(Icons.lock),
            border: const OutlineInputBorder(),
            suffixIcon: IconButton(
              icon: Icon(
                  _passwordVisible ? Icons.visibility_off : Icons.visibility),
              onPressed: () =>
                  setState(() => _passwordVisible = !_passwordVisible),
            ),
          ),
        ),
        const SizedBox(height: 24),
        FilledButton.icon(
          onPressed: _isSendingWifi ? null : _sendWifiCredentials,
          icon: _isSendingWifi
              ? const SizedBox(
                  width: 18,
                  height: 18,
                  child: CircularProgressIndicator(strokeWidth: 2),
                )
              : const Icon(Icons.send),
          label: Text(_isSendingWifi ? '전송 중...' : 'Wi-Fi 정보 전송'),
        ),
        const SizedBox(height: 12),
        TextButton(
          onPressed: () {
            context.read<BleProvider>().disconnect();
            setState(() => _showWifiForm = false);
          },
          child: const Text('다른 기기 선택'),
        ),
      ],
    );
  }
}
