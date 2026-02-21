# minBot Design Document

**Date**: 2026-02-22
**Status**: Approved
**Author**: kimminhyeok + Claude

---

## 1. Overview

소형 휴대용 애완 로봇. 데스크에 세워두거나 클립/키링으로 가방/옷에 부착 가능.
여자친구에게 선물할 목적으로 제작.

## 2. Requirements

### 필수 요구사항
1. 빠른 음성 인식 (STT)
2. 내 목소리 학습 및 흉내 (Voice Cloning)
3. 내 말투 학습 및 흉내 (LLM 프롬프트/fine-tuning)
4. 디지털 디스플레이 얼굴 (픽셀 아트 스타일, 원형 TFT)
5. 균형 감각을 위한 자이로스코프 (MPU6050)
6. 대화 친구 + 알림/비서 + 감정 동반자 복합 기능
7. USB-C 충전 (향후 Qi 무선충전 확장 가능)

### 비기능 요구사항
- 크기: 5-12cm (소형~중소형)
- 응답 지연: <500ms (스트리밍 TTS 시 첫 음절 <300ms 목표)
- 배터리: 8-12시간 대기, 2-3시간 활성 사용
- 오프라인 폴백: 기본 이모션 반응 + 사전 저장된 응답

## 3. Architecture

### 하드웨어 플랫폼
- **MCU**: ESP32-S3-WROOM-1 (8MB PSRAM)
- **오디오 입력**: INMP441 I2S MEMS 마이크
- **오디오 출력**: MAX98357A I2S 앰프 + 15mm 스피커
- **디스플레이**: GC9A01 1.28" 원형 TFT (240x240, SPI)
- **센서**: MPU6050 6축 IMU (I2C)
- **전원**: LiPo 3.7V 600mAh + TP4056 USB-C 충전 + ME6211 3.3V LDO

### 소프트웨어 아키텍처: ESP-ADF 파이프라인

```
Core 0: Wi-Fi + Network
  - WebSocket Client Task (prio 5)
  - 양방향 바이너리 오디오 스트리밍

Core 1: Audio + Display (실시간)
  - ESP-ADF Audio Pipeline (prio 23)
    [I2S Mic] →(ringbuf)→ [VAD] →(ringbuf)→ [Encoder]
    [Decoder] →(ringbuf)→ [I2S Spk]
  - Display Task (prio 10): 픽셀아트 렌더
  - Sensor Task (prio 8): MPU6050 읽기
```

### 클라우드 AI 파이프라인

| 단계 | 1순위 서비스 | 대안 | 예상 지연 |
|------|-------------|------|----------|
| STT | **Deepgram Nova-3** (진정한 스트리밍) | AssemblyAI Universal-Streaming | ~90-150ms |
| LLM | **Groq Llama 4 Scout** (460+ tok/s) | GPT-4o-mini | ~50-200ms |
| TTS | **ElevenLabs Flash v2.5** (Voice Clone) | Cartesia Sonic-3 (~40-90ms) | ~75-135ms |
| **총합** | **파이프라인 + 네트워크** | | **~300-500ms** |

> Note: Whisper API는 네이티브 스트리밍 미지원 (배치 전용). Deepgram Nova-3 권장.
> 오디오 전송: Opus 코덱 사용 (PCM 대비 80-90% 대역폭 절약)
> VAD: Silero VAD 사용 (30ms 프레임당 <1ms 처리)
> 오픈소스 TTS 대안: XTTS v2 (3초 클립으로 한국어 Voice Clone, <200ms)

### Voice Cloning & 말투 학습

- **음색 복제**: ElevenLabs Instant Voice Cloning (최소 오디오 샘플)
- **말투 학습**: Fine-tuning 우선 (연구 결과 프롬프트보다 안정적)
  - 대안: 시스템 프롬프트에 말투 패턴 주입 (프로토타입 단계)
- **오픈소스 대안**: XTTS v2 (3초 클립으로 복제, 한국어 지원, <200ms 스트리밍)

### DMA 버퍼 설정
- `dma_buf_len`: 512 samples
- `dma_buf_count`: 3
- VAD: 25ms 프레임, 10ms 오버랩

## 4. Firmware Structure

```
firmware/
├── src/
│   ├── main.cpp                 # 메인, 초기화
│   ├── audio/
│   │   ├── mic_driver.h/cpp     # I2S 마이크 (ESP-ADF element)
│   │   ├── speaker_driver.h/cpp # I2S 스피커 (ESP-ADF element)
│   │   └── wake_word.h/cpp      # ESP-SR Wake Word
│   ├── display/
│   │   ├── face_engine.h/cpp    # 픽셀아트 얼굴 렌더링
│   │   ├── emotions.h/cpp       # 감정 FSM
│   │   └── sprites.h            # 스프라이트 데이터
│   ├── sensors/
│   │   └── gyro.h/cpp           # MPU6050
│   ├── network/
│   │   ├── wifi_manager.h/cpp   # Wi-Fi 관리
│   │   └── ws_client.h/cpp      # WebSocket 클라이언트
│   ├── power/
│   │   └── battery.h/cpp        # 배터리 모니터링, 슬립
│   └── config.h                 # 핀 설정, 상수
└── platformio.ini
```

## 5. Server Structure

```
server/
├── app/
│   ├── main.py                  # FastAPI, WebSocket 엔드포인트
│   ├── pipeline/
│   │   ├── stt.py               # Whisper API
│   │   ├── llm.py               # Groq Llama 4
│   │   ├── tts.py               # ElevenLabs TTS
│   │   └── voice_clone.py       # Voice Clone 관리
│   ├── personality/
│   │   ├── speech_learner.py    # 말투 패턴 학습
│   │   └── prompt_builder.py    # 말투 프롬프트 생성
│   ├── models/
│   │   └── schemas.py           # Pydantic 모델
│   └── config.py                # API 키, 설정
├── requirements.txt
└── Dockerfile
```

## 6. Emotion Engine (FSM)

| 상태 | 트리거 | 표정 |
|------|--------|------|
| IDLE | 기본 | 눈 깜빡임 애니메이션 |
| LISTENING | Wake Word 감지 | 눈 크게 |
| THINKING | 오디오 전송 중 | 눈 돌리기 |
| SPEAKING | 응답 수신 | 입 움직임 동기화 |
| HAPPY | LLM 감정 태그 | 눈 웃음 |
| SAD | LLM 감정 태그 | 눈 처짐 |
| SURPRISED | LLM 감정 태그 | 눈 동그래짐 |
| SLEEPING | 5분 무입력 | 눈 감김, 저전력 |
| TILTED | 자이로 기울기 > 30deg | 어지러운 표정 |

## 7. Error Handling

- **네트워크 장애**: 자동 재연결 (3회, 백오프) → 로컬 폴백 모드
- **오디오 장애**: I2S 리셋, 버퍼 오버플로우 시 오래된 데이터 드롭
- **센서 장애**: 재초기화, 3회 실패 시 비활성화
- **전원**: <20% 절전모드, <5% Deep Sleep, 충전 감지 시 복귀

## 8. Testing Strategy

| 단계 | 테스트 | 도구 |
|------|--------|------|
| 펌웨어 단위 | I2S, 디스플레이, 센서 | PlatformIO Unit Test |
| 서버 단위 | STT/LLM/TTS 파이프라인 | pytest + httpx |
| 통합 | ESP32 ↔ 서버 WebSocket | 실제 하드웨어 + 서버 |
| 음성 품질 | Voice Clone 유사도, 말투 | 수동 청취 테스트 |
| 내구성 | 24시간 연속 동작, 배터리 | 장시간 구동 |
| 오프라인 | Wi-Fi 끊김 폴백 | 네트워크 차단 |

## 9. BOM (Bill of Materials)

| 부품 | 모델 | 예상 가격 |
|------|------|----------|
| MCU | ESP32-S3-WROOM-1 (8MB PSRAM) | ~8,000원 |
| 마이크 | INMP441 I2S MEMS | ~3,000원 |
| 앰프+스피커 | MAX98357A + 15mm 스피커 | ~4,000원 |
| 디스플레이 | GC9A01 1.28" 원형 TFT | ~6,000원 |
| 자이로 | MPU6050 | ~2,000원 |
| 배터리 | LiPo 3.7V 600mAh | ~3,000원 |
| 충전 IC | TP4056 USB-C | ~1,500원 |
| LDO | ME6211 3.3V | ~500원 |
| 기타 | 커넥터, 와이어, PCB, 클립 | ~5,000원 |
| 3D 프린팅 | 외주 (케이스) | ~10,000-20,000원 |
| **합계** | | **~35,000-55,000원** |

## 10. API Cost Estimates (월간)

| 서비스 | 단가 | 일 50회 대화 기준 |
|--------|------|------------------|
| Whisper API | $0.006/min | ~$0.90/월 |
| Groq Llama 4 | $0.11/M input | ~$1.65/월 |
| ElevenLabs | 크레딧 기반 | ~$5/월 (기본 플랜) |
| **합계** | | **~$7.55/월** |
