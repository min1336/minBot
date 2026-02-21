class RobotStatus {
  final int batteryPercent;
  final bool isConnected;
  final String currentEmotion;
  final int wifiRssi;
  final int uptimeSeconds;

  const RobotStatus({
    required this.batteryPercent,
    required this.isConnected,
    required this.currentEmotion,
    required this.wifiRssi,
    required this.uptimeSeconds,
  });

  factory RobotStatus.fromJson(Map<String, dynamic> json) {
    return RobotStatus(
      batteryPercent: (json['battery_percent'] as num?)?.toInt() ?? 0,
      isConnected: json['is_connected'] as bool? ?? false,
      currentEmotion: json['current_emotion'] as String? ?? 'neutral',
      wifiRssi: (json['wifi_rssi'] as num?)?.toInt() ?? -100,
      uptimeSeconds: (json['uptime_seconds'] as num?)?.toInt() ?? 0,
    );
  }

  Map<String, dynamic> toJson() => {
        'battery_percent': batteryPercent,
        'is_connected': isConnected,
        'current_emotion': currentEmotion,
        'wifi_rssi': wifiRssi,
        'uptime_seconds': uptimeSeconds,
      };

  String get emotionEmoji {
    switch (currentEmotion.toLowerCase()) {
      case 'happy':
        return '😊';
      case 'sad':
        return '😢';
      case 'excited':
        return '🤩';
      case 'sleepy':
        return '😴';
      case 'angry':
        return '😠';
      case 'curious':
        return '🤔';
      default:
        return '😐';
    }
  }

  String get uptimeFormatted {
    final hours = uptimeSeconds ~/ 3600;
    final minutes = (uptimeSeconds % 3600) ~/ 60;
    final seconds = uptimeSeconds % 60;
    if (hours > 0) return '${hours}시간 ${minutes}분';
    if (minutes > 0) return '${minutes}분 ${seconds}초';
    return '${seconds}초';
  }

  String get wifiStrength {
    if (wifiRssi >= -50) return '매우 강함';
    if (wifiRssi >= -60) return '강함';
    if (wifiRssi >= -70) return '보통';
    if (wifiRssi >= -80) return '약함';
    return '매우 약함';
  }
}
