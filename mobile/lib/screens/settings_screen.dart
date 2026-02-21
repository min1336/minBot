import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../providers/settings_provider.dart';
import '../providers/api_provider.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late TextEditingController _serverUrlController;
  late TextEditingController _wakeWordController;
  bool _isTriggeringOta = false;

  @override
  void initState() {
    super.initState();
    final settings = context.read<SettingsProvider>();
    _serverUrlController = TextEditingController(text: settings.serverUrl);
    _wakeWordController = TextEditingController(text: settings.wakeWord);
  }

  @override
  void dispose() {
    _serverUrlController.dispose();
    _wakeWordController.dispose();
    super.dispose();
  }

  Future<void> _triggerOtaUpdate() async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('OTA 업데이트'),
        content: const Text('minBot 펌웨어를 최신 버전으로 업데이트하시겠습니까?\n업데이트 중에는 로봇이 재시작됩니다.'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('취소'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('업데이트'),
          ),
        ],
      ),
    );
    if (confirmed != true) return;

    setState(() => _isTriggeringOta = true);
    final api = context.read<ApiProvider>();
    final success = await api.triggerOtaUpdate();
    if (mounted) {
      setState(() => _isTriggeringOta = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            success ? 'OTA 업데이트가 시작되었습니다.' : (api.errorMessage.isNotEmpty ? api.errorMessage : '업데이트 요청에 실패했습니다.'),
          ),
          backgroundColor:
              success ? Colors.green : Theme.of(context).colorScheme.error,
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final settings = context.watch<SettingsProvider>();

    return Scaffold(
      appBar: AppBar(
        title: const Text('설정'),
        centerTitle: true,
        backgroundColor: colorScheme.primaryContainer,
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _buildSectionHeader('서버 연결', context),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                children: [
                  TextField(
                    controller: _serverUrlController,
                    decoration: const InputDecoration(
                      labelText: '서버 URL',
                      hintText: 'http://192.168.1.100:8000',
                      prefixIcon: Icon(Icons.dns),
                      border: OutlineInputBorder(),
                    ),
                    keyboardType: TextInputType.url,
                    onSubmitted: (v) =>
                        context.read<SettingsProvider>().setServerUrl(v.trim()),
                    onEditingComplete: () => context
                        .read<SettingsProvider>()
                        .setServerUrl(_serverUrlController.text.trim()),
                  ),
                  const SizedBox(height: 8),
                  Align(
                    alignment: Alignment.centerRight,
                    child: TextButton(
                      onPressed: () => context
                          .read<SettingsProvider>()
                          .setServerUrl(_serverUrlController.text.trim()),
                      child: const Text('저장'),
                    ),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 16),
          _buildSectionHeader('음성 인식', context),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                children: [
                  TextField(
                    controller: _wakeWordController,
                    decoration: const InputDecoration(
                      labelText: '웨이크 워드',
                      hintText: '민봇아',
                      prefixIcon: Icon(Icons.record_voice_over),
                      border: OutlineInputBorder(),
                    ),
                    onSubmitted: (v) =>
                        context.read<SettingsProvider>().setWakeWord(v.trim()),
                    onEditingComplete: () => context
                        .read<SettingsProvider>()
                        .setWakeWord(_wakeWordController.text.trim()),
                  ),
                  const SizedBox(height: 8),
                  Align(
                    alignment: Alignment.centerRight,
                    child: TextButton(
                      onPressed: () => context
                          .read<SettingsProvider>()
                          .setWakeWord(_wakeWordController.text.trim()),
                      child: const Text('저장'),
                    ),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 16),
          _buildSectionHeader('감정 & 외관', context),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    '감정 민감도',
                    style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                          color: colorScheme.onSurfaceVariant,
                        ),
                  ),
                  Row(
                    children: [
                      const Icon(Icons.sentiment_neutral, size: 20),
                      Expanded(
                        child: Slider(
                          value: settings.emotionSensitivity,
                          min: 0.0,
                          max: 1.0,
                          divisions: 10,
                          label: '${(settings.emotionSensitivity * 100).round()}%',
                          onChanged: (v) => context
                              .read<SettingsProvider>()
                              .setEmotionSensitivity(v),
                        ),
                      ),
                      const Icon(Icons.sentiment_very_satisfied, size: 20),
                    ],
                  ),
                  const Divider(height: 24),
                  SwitchListTile(
                    contentPadding: EdgeInsets.zero,
                    title: const Text('다크 모드'),
                    subtitle: const Text('어두운 테마를 사용합니다'),
                    secondary: const Icon(Icons.dark_mode),
                    value: settings.darkMode,
                    onChanged: (v) =>
                        context.read<SettingsProvider>().setDarkMode(v),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 16),
          _buildSectionHeader('시스템', context),
          Card(
            child: Column(
              children: [
                ListTile(
                  leading: const Icon(Icons.system_update),
                  title: const Text('OTA 펌웨어 업데이트'),
                  subtitle: const Text('최신 버전으로 업데이트합니다'),
                  trailing: _isTriggeringOta
                      ? const SizedBox(
                          width: 24,
                          height: 24,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.chevron_right),
                  onTap: _isTriggeringOta ? null : _triggerOtaUpdate,
                ),
                const Divider(height: 1),
                ListTile(
                  leading: const Icon(Icons.info_outline),
                  title: const Text('앱 버전'),
                  trailing: Text(
                    '1.0.0',
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                          color: colorScheme.onSurfaceVariant,
                        ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 32),
        ],
      ),
    );
  }

  Widget _buildSectionHeader(String title, BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(left: 4, bottom: 8),
      child: Text(
        title,
        style: Theme.of(context).textTheme.labelLarge?.copyWith(
              color: Theme.of(context).colorScheme.primary,
              fontWeight: FontWeight.bold,
            ),
      ),
    );
  }
}
