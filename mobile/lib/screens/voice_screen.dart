import 'dart:io';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:record/record.dart';
import 'package:path_provider/path_provider.dart';

import '../providers/api_provider.dart';
import '../models/personality_config.dart';

class VoiceScreen extends StatefulWidget {
  const VoiceScreen({super.key});

  @override
  State<VoiceScreen> createState() => _VoiceScreenState();
}

class _VoiceScreenState extends State<VoiceScreen> {
  final AudioRecorder _recorder = AudioRecorder();
  final List<_RecordedSample> _samples = [];
  PersonalityConfig? _personality;
  String _selectedProvider = 'elevenlabs';
  bool _isRecording = false;
  bool _isUploading = false;
  bool _isLoadingPersonality = false;
  String _newExpression = '';
  final _expressionController = TextEditingController();

  static const List<Map<String, String>> _providers = [
    {'value': 'elevenlabs', 'label': 'ElevenLabs Flash v2.5'},
    {'value': 'xtts', 'label': 'XTTS v2 (오픈소스)'},
    {'value': 'cosyvoice', 'label': 'CosyVoice'},
  ];

  @override
  void initState() {
    super.initState();
    _loadPersonality();
  }

  @override
  void dispose() {
    _recorder.dispose();
    _expressionController.dispose();
    super.dispose();
  }

  Future<void> _loadPersonality() async {
    setState(() => _isLoadingPersonality = true);
    final api = context.read<ApiProvider>();
    final config = await api.getPersonality();
    if (mounted) {
      setState(() {
        _personality = config ?? PersonalityConfig();
        _isLoadingPersonality = false;
      });
    }
  }

  Future<void> _toggleRecording() async {
    if (_isRecording) {
      final path = await _recorder.stop();
      setState(() => _isRecording = false);
      if (path != null) {
        final file = File(path);
        final stat = await file.stat();
        final durationSec = stat.size ~/ 32000;
        setState(() {
          _samples.add(_RecordedSample(
            file: file,
            durationSeconds: durationSec.clamp(1, 999),
            label: '샘플 ${_samples.length + 1}',
          ));
        });
      }
      return;
    }

    final hasPermission = await _recorder.hasPermission();
    if (!hasPermission) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('마이크 권한이 필요합니다.')),
        );
      }
      return;
    }

    final dir = await getTemporaryDirectory();
    final path =
        '${dir.path}/voice_sample_${DateTime.now().millisecondsSinceEpoch}.m4a';
    await _recorder.start(
      const RecordConfig(encoder: AudioEncoder.aacLc, sampleRate: 16000),
      path: path,
    );
    setState(() => _isRecording = true);
  }

  Future<void> _uploadSamples() async {
    if (_samples.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('먼저 음성 샘플을 녹음해주세요.')),
      );
      return;
    }
    setState(() => _isUploading = true);
    final api = context.read<ApiProvider>();
    final files = _samples.map((s) => s.file).toList();
    final success = await api.uploadVoiceSamples(files);
    if (mounted) {
      setState(() => _isUploading = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(success ? '음성 샘플이 업로드되었습니다!' : api.errorMessage),
          backgroundColor:
              success ? Colors.green : Theme.of(context).colorScheme.error,
        ),
      );
    }
  }

  Future<void> _changeProvider(String provider) async {
    setState(() => _selectedProvider = provider);
    final api = context.read<ApiProvider>();
    await api.changeVoiceProvider(provider);
  }

  void _addExpression() {
    final text = _expressionController.text.trim();
    if (text.isEmpty || _personality == null) return;
    setState(() {
      _personality!.favoriteExpressions.add(text);
      _expressionController.clear();
    });
    _savePersonality();
  }

  void _removeExpression(int index) {
    if (_personality == null) return;
    setState(() => _personality!.favoriteExpressions.removeAt(index));
    _savePersonality();
  }

  Future<void> _savePersonality() async {
    if (_personality == null) return;
    final api = context.read<ApiProvider>();
    await api.updatePersonality(_personality!);
  }

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;

    return Scaffold(
      appBar: AppBar(
        title: const Text('음성 설정'),
        centerTitle: true,
        backgroundColor: colorScheme.primaryContainer,
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _buildRecordSection(colorScheme),
          const SizedBox(height: 16),
          _buildProviderSection(colorScheme),
          const SizedBox(height: 16),
          _buildPersonalitySection(colorScheme),
        ],
      ),
    );
  }

  Widget _buildRecordSection(ColorScheme colorScheme) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text(
              '음성 샘플 녹음',
              style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
            ),
            const SizedBox(height: 8),
            Text(
              '3분 이상 자연스럽게 말하는 내용을 녹음해주세요.',
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: colorScheme.onSurfaceVariant,
                  ),
            ),
            const SizedBox(height: 16),
            Center(
              child: GestureDetector(
                onTap: _toggleRecording,
                child: AnimatedContainer(
                  duration: const Duration(milliseconds: 300),
                  width: 80,
                  height: 80,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: _isRecording ? colorScheme.error : colorScheme.primary,
                    boxShadow: _isRecording
                        ? [
                            BoxShadow(
                              color: colorScheme.error.withOpacity(0.4),
                              blurRadius: 20,
                              spreadRadius: 4,
                            )
                          ]
                        : [],
                  ),
                  child: Icon(
                    _isRecording ? Icons.stop : Icons.mic,
                    color: colorScheme.onPrimary,
                    size: 36,
                  ),
                ),
              ),
            ),
            const SizedBox(height: 8),
            Text(
              _isRecording ? '녹음 중... (탭하여 중지)' : '탭하여 녹음 시작',
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: _isRecording ? colorScheme.error : colorScheme.onSurfaceVariant,
                  ),
            ),
            if (_samples.isNotEmpty) ...[
              const SizedBox(height: 16),
              const Divider(),
              const SizedBox(height: 8),
              ..._samples.asMap().entries.map((entry) {
                final i = entry.key;
                final sample = entry.value;
                return ListTile(
                  contentPadding: EdgeInsets.zero,
                  leading: const Icon(Icons.audio_file),
                  title: Text(sample.label),
                  subtitle: Text('${sample.durationSeconds}초'),
                  trailing: IconButton(
                    icon: const Icon(Icons.delete_outline),
                    onPressed: () => setState(() => _samples.removeAt(i)),
                  ),
                );
              }),
              const SizedBox(height: 12),
              FilledButton.icon(
                onPressed: _isUploading ? null : _uploadSamples,
                icon: _isUploading
                    ? const SizedBox(
                        width: 18,
                        height: 18,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : const Icon(Icons.cloud_upload),
                label: Text(_isUploading ? '업로드 중...' : '${_samples.length}개 샘플 업로드'),
              ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _buildProviderSection(ColorScheme colorScheme) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              '음성 합성 엔진',
              style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
            ),
            const SizedBox(height: 12),
            ..._providers.map((provider) => RadioListTile<String>(
                  contentPadding: EdgeInsets.zero,
                  title: Text(provider['label']!),
                  value: provider['value']!,
                  groupValue: _selectedProvider,
                  onChanged: (v) => v != null ? _changeProvider(v) : null,
                )),
          ],
        ),
      ),
    );
  }

  Widget _buildPersonalitySection(ColorScheme colorScheme) {
    if (_isLoadingPersonality) {
      return const Card(
        child: Padding(
          padding: EdgeInsets.all(24),
          child: Center(child: CircularProgressIndicator()),
        ),
      );
    }
    final personality = _personality;
    if (personality == null) return const SizedBox.shrink();

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              '말투 개성 설정',
              style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
            ),
            const SizedBox(height: 4),
            Text(
              '즐겨 쓰는 표현을 추가하면 minBot이 따라 사용해요.',
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: colorScheme.onSurfaceVariant,
                  ),
            ),
            const SizedBox(height: 16),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _expressionController,
                    decoration: const InputDecoration(
                      hintText: '예: 진짜요? 대박!',
                      border: OutlineInputBorder(),
                      contentPadding:
                          EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                    ),
                    onSubmitted: (_) => _addExpression(),
                  ),
                ),
                const SizedBox(width: 8),
                FilledButton(
                  onPressed: _addExpression,
                  child: const Text('추가'),
                ),
              ],
            ),
            const SizedBox(height: 12),
            if (personality.favoriteExpressions.isEmpty)
              Text(
                '아직 추가된 표현이 없습니다.',
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: colorScheme.onSurfaceVariant,
                    ),
              )
            else
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: personality.favoriteExpressions
                    .asMap()
                    .entries
                    .map((entry) => Chip(
                          label: Text(entry.value),
                          onDeleted: () => _removeExpression(entry.key),
                          deleteIcon: const Icon(Icons.close, size: 16),
                        ))
                    .toList(),
              ),
          ],
        ),
      ),
    );
  }
}

class _RecordedSample {
  final File file;
  final int durationSeconds;
  final String label;

  const _RecordedSample({
    required this.file,
    required this.durationSeconds,
    required this.label,
  });
}
