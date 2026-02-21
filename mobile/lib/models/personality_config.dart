class PersonalityConfig {
  String botName;
  List<String> speechPatterns;
  List<String> sentenceEndings;
  List<String> favoriteExpressions;
  List<String> personalityTraits;

  PersonalityConfig({
    this.botName = 'minBot',
    List<String>? speechPatterns,
    List<String>? sentenceEndings,
    List<String>? favoriteExpressions,
    List<String>? personalityTraits,
  })  : speechPatterns = speechPatterns ?? [],
        sentenceEndings = sentenceEndings ?? [],
        favoriteExpressions = favoriteExpressions ?? [],
        personalityTraits = personalityTraits ?? [];

  factory PersonalityConfig.fromJson(Map<String, dynamic> json) {
    return PersonalityConfig(
      botName: json['bot_name'] as String? ?? 'minBot',
      speechPatterns: _toStringList(json['speech_patterns']),
      sentenceEndings: _toStringList(json['sentence_endings']),
      favoriteExpressions: _toStringList(json['favorite_expressions']),
      personalityTraits: _toStringList(json['personality_traits']),
    );
  }

  Map<String, dynamic> toJson() => {
        'bot_name': botName,
        'speech_patterns': speechPatterns,
        'sentence_endings': sentenceEndings,
        'favorite_expressions': favoriteExpressions,
        'personality_traits': personalityTraits,
      };

  PersonalityConfig copyWith({
    String? botName,
    List<String>? speechPatterns,
    List<String>? sentenceEndings,
    List<String>? favoriteExpressions,
    List<String>? personalityTraits,
  }) {
    return PersonalityConfig(
      botName: botName ?? this.botName,
      speechPatterns: speechPatterns ?? List.from(this.speechPatterns),
      sentenceEndings: sentenceEndings ?? List.from(this.sentenceEndings),
      favoriteExpressions:
          favoriteExpressions ?? List.from(this.favoriteExpressions),
      personalityTraits:
          personalityTraits ?? List.from(this.personalityTraits),
    );
  }

  static List<String> _toStringList(dynamic value) {
    if (value == null) return [];
    if (value is List) return value.map((e) => e.toString()).toList();
    return [];
  }
}
