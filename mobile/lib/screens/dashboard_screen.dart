import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../providers/api_provider.dart';
import '../providers/ble_provider.dart';
import '../models/robot_status.dart';

class DashboardScreen extends StatefulWidget {
  const DashboardScreen({super.key});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  int _selectedIndex = 0;
  RobotStatus? _status;
  List<Map<String, dynamic>> _conversations = [];
  bool _isRefreshing = false;

  @override
  void initState() {
    super.initState();
    _refresh();
  }

  Future<void> _refresh() async {
    if (_isRefreshing) return;
    setState(() => _isRefreshing = true);
    final api = context.read<ApiProvider>();
    final status = await api.getStatus();
    final conversations = await api.getConversations(limit: 20);
    if (mounted) {
      setState(() {
        _status = status;
        _conversations = conversations;
        _isRefreshing = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;

    return Scaffold(
      appBar: AppBar(
        title: const Text('minBot'),
        centerTitle: true,
        backgroundColor: colorScheme.primaryContainer,
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh),
            onPressed: _isRefreshing ? null : _refresh,
            tooltip: '새로고침',
          ),
        ],
      ),
      body: RefreshIndicator(
        onRefresh: _refresh,
        child: _buildBody(colorScheme),
      ),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _selectedIndex,
        onDestinationSelected: (index) {
          setState(() => _selectedIndex = index);
          if (index == 1) Navigator.pushNamed(context, '/voice');
          if (index == 2) Navigator.pushNamed(context, '/settings');
        },
        destinations: const [
          NavigationDestination(
            icon: Icon(Icons.home_outlined),
            selectedIcon: Icon(Icons.home),
            label: '홈',
          ),
          NavigationDestination(
            icon: Icon(Icons.mic_outlined),
            selectedIcon: Icon(Icons.mic),
            label: '음성',
          ),
          NavigationDestination(
            icon: Icon(Icons.settings_outlined),
            selectedIcon: Icon(Icons.settings),
            label: '설정',
          ),
        ],
      ),
    );
  }

  Widget _buildBody(ColorScheme colorScheme) {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _buildStatusCard(colorScheme),
        const SizedBox(height: 16),
        _buildConversationsSection(colorScheme),
      ],
    );
  }

  Widget _buildStatusCard(ColorScheme colorScheme) {
    final ble = context.watch<BleProvider>();
    final status = _status;

    return Card(
      elevation: 2,
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Text(
                  status?.emotionEmoji ?? '🤖',
                  style: const TextStyle(fontSize: 48),
                ),
                const SizedBox(width: 16),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'minBot',
                        style:
                            Theme.of(context).textTheme.headlineSmall?.copyWith(
                                  fontWeight: FontWeight.bold,
                                  color: colorScheme.onSurface,
                                ),
                      ),
                      const SizedBox(height: 4),
                      Row(
                        children: [
                          Icon(
                            ble.isConnected
                                ? Icons.bluetooth_connected
                                : Icons.bluetooth_disabled,
                            size: 16,
                            color: ble.isConnected
                                ? colorScheme.primary
                                : colorScheme.error,
                          ),
                          const SizedBox(width: 4),
                          Text(
                            ble.isConnected ? '연결됨' : '연결 끊김',
                            style:
                                Theme.of(context).textTheme.bodySmall?.copyWith(
                                      color: ble.isConnected
                                          ? colorScheme.primary
                                          : colorScheme.error,
                                    ),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ],
            ),
            const SizedBox(height: 20),
            if (status != null) ...[
              _buildStatusRow(
                Icons.battery_charging_full,
                '배터리',
                '${status.batteryPercent}%',
                colorScheme,
                trailing: LinearProgressIndicator(
                  value: status.batteryPercent / 100,
                  backgroundColor: colorScheme.surfaceVariant,
                  color: status.batteryPercent > 20
                      ? colorScheme.primary
                      : colorScheme.error,
                ),
              ),
              const SizedBox(height: 12),
              _buildStatusRow(
                Icons.wifi,
                'Wi-Fi 신호',
                '${status.wifiStrength} (${status.wifiRssi} dBm)',
                colorScheme,
              ),
              const SizedBox(height: 12),
              _buildStatusRow(
                Icons.timer,
                '가동 시간',
                status.uptimeFormatted,
                colorScheme,
              ),
            ] else
              Center(
                child: Text(
                  _isRefreshing ? '상태 확인 중...' : '상태를 불러올 수 없습니다.',
                  style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                        color: colorScheme.onSurfaceVariant,
                      ),
                ),
              ),
          ],
        ),
      ),
    );
  }

  Widget _buildStatusRow(
    IconData icon,
    String label,
    String value,
    ColorScheme colorScheme, {
    Widget? trailing,
  }) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Icon(icon, size: 18, color: colorScheme.primary),
            const SizedBox(width: 8),
            Text(
              label,
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: colorScheme.onSurfaceVariant,
                  ),
            ),
            const Spacer(),
            Text(
              value,
              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                    fontWeight: FontWeight.w600,
                  ),
            ),
          ],
        ),
        if (trailing != null) ...[
          const SizedBox(height: 6),
          trailing,
        ],
      ],
    );
  }

  Widget _buildConversationsSection(ColorScheme colorScheme) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 4),
          child: Text(
            '최근 대화',
            style: Theme.of(context).textTheme.titleMedium?.copyWith(
                  fontWeight: FontWeight.bold,
                ),
          ),
        ),
        const SizedBox(height: 12),
        if (_conversations.isEmpty)
          Card(
            child: Padding(
              padding: const EdgeInsets.all(24),
              child: Center(
                child: Column(
                  children: [
                    Icon(Icons.chat_bubble_outline,
                        size: 40, color: colorScheme.onSurfaceVariant),
                    const SizedBox(height: 8),
                    Text(
                      '아직 대화 기록이 없습니다.',
                      style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                            color: colorScheme.onSurfaceVariant,
                          ),
                    ),
                  ],
                ),
              ),
            ),
          )
        else
          ...(_conversations.map((conv) => _buildConversationTile(conv, colorScheme))),
      ],
    );
  }

  Widget _buildConversationTile(
      Map<String, dynamic> conv, ColorScheme colorScheme) {
    final userMsg = conv['user_message'] as String? ?? '';
    final botMsg = conv['bot_response'] as String? ?? '';
    final timestamp = conv['timestamp'] as String? ?? '';

    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            if (timestamp.isNotEmpty)
              Text(
                timestamp,
                style: Theme.of(context).textTheme.labelSmall?.copyWith(
                      color: colorScheme.onSurfaceVariant,
                    ),
              ),
            const SizedBox(height: 6),
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Icon(Icons.person, size: 16, color: colorScheme.primary),
                const SizedBox(width: 6),
                Expanded(
                  child: Text(
                    userMsg,
                    style: Theme.of(context).textTheme.bodyMedium,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 6),
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Icon(Icons.smart_toy, size: 16, color: colorScheme.secondary),
                const SizedBox(width: 6),
                Expanded(
                  child: Text(
                    botMsg,
                    style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                          color: colorScheme.onSurfaceVariant,
                        ),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
