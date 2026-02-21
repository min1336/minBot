# minBot - 소형 애완 로봇 프로젝트

## 프로젝트 개요
여자친구에게 선물할 소형 휴대용 애완 로봇. 데스크에 세워두거나 클립/키링으로 가방/옷에 부착 가능.

## 확정된 요구사항
1. 빠른 음성 인식 (STT)
2. 내 목소리 학습 및 흉내 (Voice Cloning - ElevenLabs Instant Clone)
3. 내 말투 학습 및 흉내 (Fine-tuning 우선, 프롬프트 대안)
4. 디지털 디스플레이 얼굴 (픽셀 아트 스타일, GC9A01 원형 TFT)
5. 균형 감각을 위한 자이로스코프 (MPU6050)
6. 대화 친구 + 알림/비서 + 감정 동반자 복합 기능
7. USB-C 충전 (향후 Qi 무선충전 확장 가능)

## 확정된 아키텍처: ESP-ADF 파이프라인
- **메인 MCU**: ESP32-S3-WROOM-1 (8MB PSRAM)
- **펌웨어**: C++ (ESP-IDF + ESP-ADF)
- **서버**: Python (FastAPI)
- **통신**: WebSocket binary (양방향 오디오 스트리밍)
- **AI 처리**: 하이브리드 (로컬 Wake Word + 클라우드 STT/LLM/TTS)
- **크기 목표**: 5-12cm (소형~중소형)

### FreeRTOS 태스크 구조
- Core 0: Wi-Fi + WebSocket Client (prio 5)
- Core 1: ESP-ADF Audio Pipeline (prio 23), Display (prio 10), Sensor (prio 8)
- DMA: buf_len=512, buf_count=3
- VAD: 25ms 프레임, 10ms 오버랩

### 클라우드 AI 파이프라인 (~300-500ms 총 지연)
- STT: Deepgram Nova-3 (진정한 스트리밍) ~90-150ms (대안: AssemblyAI)
  - Note: Whisper API는 배치 전용, 스트리밍 미지원
- LLM: Groq Llama 4 Scout (460+ tok/s) ~50-200ms
- TTS: ElevenLabs Flash v2.5 (Voice Clone) ~75-135ms (대안: Cartesia Sonic-3 ~40-90ms)
- 오디오 코덱: Opus (전송), PCM 16bit 16kHz (API)
- VAD: Silero VAD (30ms 프레임, <1ms 처리)
- 오픈소스 TTS 대안: XTTS v2 (3초 클립, 한국어, <200ms)

## 확정된 하드웨어 BOM (~35,000-55,000원)
| 부품 | 모델 | 역할 |
|------|------|------|
| 메인 MCU | ESP32-S3-WROOM-1 (8MB PSRAM) | 모든 제어 + Wi-Fi |
| 마이크 | INMP441 (I2S MEMS) | 음성 입력 |
| 앰프+스피커 | MAX98357A + 15mm 스피커 | 음성 출력 |
| 디스플레이 | GC9A01 1.28" 원형 TFT (240x240) | 픽셀 아트 얼굴 |
| 자이로스코프 | MPU6050 (6축 IMU) | 기울기/움직임 감지 |
| 배터리 | LiPo 3.7V 600mAh | 전원 |
| 충전 IC | TP4056 USB-C 모듈 | 배터리 충전 관리 |
| 전압 레귤레이터 | ME6211 3.3V LDO | 안정적 전원 공급 |

## 설계 문서
- 전체 설계: `docs/plans/2026-02-22-minbot-design.md`

## 개발 상태
- [x] 요구사항 확정
- [x] 아키텍처 확정 (ESP-ADF 파이프라인)
- [x] 하드웨어 BOM 확정
- [x] 소프트웨어 아키텍처 확정 (ESP-ADF + FreeRTOS 듀얼코어)
- [x] AI 파이프라인 확정 (Deepgram Nova-3 + Groq Llama 4 + ElevenLabs)
- [x] 에러 처리 전략 확정
- [x] 테스트 전략 확정
- [x] 설계 문서 작성 완료
- [x] 구현 계획 수립
- **Phase 1: 서버 (Python FastAPI)** ✅ 완료
  - [x] Step 1.1: 프로젝트 초기화 (디렉토리, 의존성, config)
  - [x] Step 1.2: STT 모듈 (Deepgram Nova-3 스트리밍)
  - [x] Step 1.3: LLM 모듈 (Groq Llama 4 Scout 스트리밍)
  - [x] Step 1.4: TTS 모듈 (ElevenLabs Flash v2.5 스트리밍)
  - [x] Step 1.5: Voice Clone 멀티 프로바이더 (ElevenLabs/XTTS/CosyVoice)
  - [x] Step 1.6: 말투 학습 시스템 (패턴 추출 + 프롬프트 빌더)
  - [x] Step 1.7: WebSocket 엔드포인트 (전체 파이프라인 연결 + Barge-in)
  - [x] Step 1.8: REST API (JWT 인증 + 모바일 앱용 엔드포인트)
  - [x] Step 1.9: 테스트 (8개 테스트 파일)
  - [x] 보안 코드 리뷰 수정 (CRITICAL 2건 + HIGH 4건)
- **Phase 2: 펌웨어 (ESP32-S3)** 🔄 진행 중
  - [ ] Step 2.1-2.9
- **Phase 3: 모바일 앱 (Flutter)** ⏳ 대기
- **Phase 4: 통합 + 하드웨어** ⏳ 대기

## 서버 구조 (구현 완료)
```
server/
├── app/
│   ├── main.py              # FastAPI + WebSocket /audio + 전체 파이프라인
│   ├── config.py            # pydantic-settings 환경변수 관리
│   ├── pipeline/
│   │   ├── stt.py           # Deepgram Nova-3 스트리밍 STT
│   │   ├── llm.py           # Groq Llama 4 Scout 스트리밍 LLM
│   │   ├── tts.py           # ElevenLabs Flash v2.5 스트리밍 TTS
│   │   ├── voice_clone.py   # VoiceCloneProvider 추상 인터페이스
│   │   └── providers/       # ElevenLabs, XTTS v2, CosyVoice 2
│   ├── personality/
│   │   ├── speech_learner.py  # 한국어 말투 패턴 학습
│   │   └── prompt_builder.py  # 동적 시스템 프롬프트 생성
│   ├── api/
│   │   ├── auth.py          # JWT 인증
│   │   └── routes.py        # REST API 엔드포인트
│   └── models/schemas.py    # Pydantic 모델
├── tests/                   # pytest 테스트 스위트
├── requirements.txt
└── .env.example
```

## 주요 기술 결정 사항
- **Barge-in**: 서버에서 cancel 메시지 전송 → ESP32 스피커 출력 즉시 중단
- **Voice Clone**: 프로바이더 추상화로 ElevenLabs/XTTS/CosyVoice 교체 가능
- **VAD**: Silero VAD 우선, 미설치 시 RMS 에너지 기반 폴백
- **보안**: 환경변수 필수 (admin_password, jwt_secret_key), 파일 업로드 경로 검증
