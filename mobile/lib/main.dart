import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'providers/ble_provider.dart';
import 'providers/api_provider.dart';
import 'providers/settings_provider.dart';
import 'screens/splash_screen.dart';
import 'screens/connect_screen.dart';
import 'screens/dashboard_screen.dart';
import 'screens/voice_screen.dart';
import 'screens/settings_screen.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  final settingsProvider = SettingsProvider();
  await settingsProvider.init();

  runApp(
    MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => settingsProvider),
        ChangeNotifierProvider(create: (_) => BleProvider()),
        ChangeNotifierProxyProvider<SettingsProvider, ApiProvider>(
          create: (_) => ApiProvider(),
          update: (_, settings, api) {
            api?.updateBaseUrl(settings.serverUrl);
            return api ?? ApiProvider();
          },
        ),
      ],
      child: const MinBotApp(),
    ),
  );
}

class MinBotApp extends StatelessWidget {
  const MinBotApp({super.key});

  @override
  Widget build(BuildContext context) {
    final settings = context.watch<SettingsProvider>();

    return MaterialApp(
      title: 'minBot',
      debugShowCheckedModeBanner: false,
      locale: const Locale('ko', 'KR'),
      themeMode: settings.darkMode ? ThemeMode.dark : ThemeMode.light,
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.deepPurple,
          brightness: Brightness.light,
        ),
        fontFamily: 'sans-serif',
      ),
      darkTheme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.deepPurple,
          brightness: Brightness.dark,
        ),
        fontFamily: 'sans-serif',
      ),
      initialRoute: '/',
      routes: {
        '/': (_) => const SplashScreen(),
        '/connect': (_) => const ConnectScreen(),
        '/dashboard': (_) => const DashboardScreen(),
        '/voice': (_) => const VoiceScreen(),
        '/settings': (_) => const SettingsScreen(),
      },
    );
  }
}
