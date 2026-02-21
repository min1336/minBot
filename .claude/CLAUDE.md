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
- [x] AI 파이프라인 확정 (Whisper + Groq Llama 4 + ElevenLabs)
- [x] 에러 처리 전략 확정
- [x] 테스트 전략 확정
- [x] 설계 문서 작성 완료
- [ ] 구현 계획 수립
- [ ] 구현
