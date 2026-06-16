# 진행 상황 / 다음 작업 핸드오프

> 마지막 업데이트: 2026-06-16. 다음 세션에서 이 문서부터 읽고 이어서 진행하면 됩니다.

## ✅ 이번 세션에서 끝낸 것 (Stage 1)

PlatformIO 스캐폴드 + ST7796 디스플레이 최소 구동 코드 작성 완료.
**빌드 검증 통과** (`pio run -e tdisplay-s3-pro` → SUCCESS, 경고 0).
**아직 실제 하드웨어에 업로드해서 화면을 확인하지는 못했음** (보드 미연결).

빌드 검증 메모:
- LovyanGFX는 `^1.1.16` → 실제 **1.2.21** 설치됨. 우려했던 `Light_PWM` API 호환성 문제 없음.
- `color888()` -Woverflow 경고 → `rgb(0xRRGGBB)` 헬퍼로 채널 마스킹해 정리.
- `LGFX_USE_V1` 재정의 경고 → 헤더 정의를 `#ifndef` 가드로 감쌈 (platformio.ini가 이미 전역 정의).
- PlatformIO는 전용 venv에 설치됨: `~/.pio-venv/Scripts/pio.exe`
  (PLATFORMIO_CORE_DIR=`~/.platformio`). PATH엔 없으니 호출 시 전체 경로 사용.

작성된 파일:

| 파일 | 내용 |
|------|------|
| `platformio.ini` | env `tdisplay-s3-pro`, ESP32-S3R8 (16MB flash / OPI PSRAM), `default_16MB.csv`, lib_deps에 LovyanGFX 핀 고정 |
| `src/board_pins.h` | **공식 레포에서 검증한** GPIO 핀맵 (아래 표 참고) |
| `src/display/LGFX_TDisplayS3Pro.hpp` | LovyanGFX 디바이스(버스/패널/백라이트) — 순수 드라이버 레이어 |
| `src/display/DisplayHAL.h/.cpp` | 드로잉 HAL (UI는 이 레이어만 호출, LVGL 확장 대비 분리) |
| `src/main.cpp` | `setup()`에서 display init → 테스트 화면(타이틀 + 65% 컬러 바) |
| `README.md` | 빌드/업로드/모니터 방법 |
| `.gitignore` | `.pio/` 등 |

## ✅ Stage 1 완전 종료 (실기 검증 완료)

1. ~~실기 빌드 검증~~ ✅ 완료.
2. ~~업로드 후 화면 확인~~ ✅ 완료 — COM6에 업로드, 부팅 로그 정상
   (`PSRAM enabled` → `[display] init OK` → `test screen drawn`).
   실제 화면 사진으로 확인: 480×222 가로 화면에 타이틀 + 코랄색 65% 바가
   중앙 정렬로 정상 표시. **offset_x=49 / setRotation(1) / invert=true 전부 정확,
   조정 불필요.**

업로드/모니터 명령 (보드 COM6 연결 시):
```
~/.pio-venv/Scripts/pio.exe run -e tdisplay-s3-pro -t upload --upload-port COM6
```
> 주의: 이 펌웨어는 USB CDC(`ARDUINO_USB_CDC_ON_BOOT=1`)로 로그를 출력하므로,
> 리셋 직후 포트가 재열거됨. 시리얼 모니터는 리셋 후 곧바로 열어야 앱 로그가 잡힘.

## ✅ Stage 2 — 완전 종료 (E2E 실기 검증 완료)

WiFi 캡티브 포털 프로비저닝 + 토큰 암호화 저장 + PIN 언락까지 구현·빌드·업로드 완료.
실기 E2E 검증: 셋업 폼 제출 → 재부팅 → 집 WiFi 자동접속 → 웹 언락(LAN IP)에서
PIN 입력 → 상태 화면(WiFi/IP/Signal) 전환까지 전 과정 정상 동작 확인.

운영 메모(삽질 끝에 확정된 사실):
- **ESP32-S3는 2.4GHz 전용** — 5GHz SSID에는 절대 연결 안 됨. 셋업 시 2.4GHz SSID 필수.
- **`claude setup-token`은 Claude Code 안의 `!`로 실행하면 백그라운드로 밀려 토큰이
  안 보임.** 반드시 **별도 일반 터미널 창**에서 실행해 토큰(`sk-ant-oat...`)을 받을 것.
- **AP 비밀번호는 `src/secrets.h`**(git 제외)에 둠. `secrets.example.h` 복사해서 생성.
  기본 fallback은 `change-me-strong-pw` — 반드시 교체.
- 폰 연결 시 리셋되던 문제 → **WiFi TX 출력 11dBm로 낮춰 해결**(브라운아웃 완화, `Net.cpp`).
- WiFi 접속 실패 시 자동으로 셋업 모드 복귀(잘못된 비번/SSID 입력해도 안전).

설계 확정 사항:
- **자격증명**: OAuth 토큰 (`claude setup-token`으로 발급, 포털 폼에 붙여넣기).
- **PIN 입력(부팅 시)**: 웹 언락 페이지 방식. WiFi 연결 후 LAN IP로 접속해 PIN 입력.
- **암호화 모델**(`src/secure/`):
  - WiFi 자격증명 → **기기 키**(MAC 유도 SHA-256)로 봉인 → 부팅 시 자동 접속.
  - OAuth 토큰 → **PIN 키**(PBKDF2-HMAC-SHA256 10k) 봉인 → 언락 필요. PIN 비저장.
  - 틀린 PIN은 AES-GCM 태그 검증 실패. 10회 실패 시 전체 와이프 → 셋업 복귀.
- **포털 HTML**: PROGMEM 내장 (별도 파일시스템 없음).
- 추가 lib 없음 (mbedtls·WiFi·DNSServer·WebServer 전부 코어 내장).

파일: `src/config.h`, `src/secure/{Crypto,Storage,CredentialStore}.*`,
`src/net/{Net,Portal}.*`, `DisplayHAL` 화면 3종, `main.cpp` 부팅 상태머신.

남은 일 (수동 E2E 테스트): 폰으로 AP 접속 → 폼 제출 → 재부팅·WiFi 접속 →
언락 페이지에서 PIN → connected 화면. 실제 OAuth 토큰 필요.

> 보안 주의(추후 하드닝): flash 암호화(eFuse) 미적용 상태에서는 기기 키가 난독화
> 수준. WiFi 비번 at-rest 보호를 강화하려면 ESP32 flash encryption 활성화 필요.

## ⏭️ 이후 단계

- **Stage 3**: Anthropic Messages 엔드포인트에 `max_tokens:1` 최소 요청,
  응답 헤더 파싱:
  - `anthropic-ratelimit-unified-5h-utilization` → Current(5h)
  - `anthropic-ratelimit-unified-7d-utilization` → Weekly(7d)
  - 리셋 시각 헤더 → 카운트다운 (**실제 응답으로 헤더명 검증 필요**)
  - 폴링 주기 설정값(기본 60초).
- **Stage 4**: 풀 UI 레이아웃 — 사용률 바 2개, 리셋 카운트다운, WiFi 신호, 배터리(SY6970).
- **Stage 5(선택)**: LVGL 도입 (HAL 레이어 이미 분리해둠 → `display::gfx()`로 flush 콜백 연결).

## 📌 검증된 하드웨어 사실 (재조사 불필요 — 공식 레포 출처)

출처: `Xinyuan-LilyGO/T-Display-S3-Pro` 의 `examples/*/utilities.h`,
`board/t-display-s3-pro.json`, 그리고 LovyanGFX Discussion #674.

핀맵:

| 기능 | GPIO |
|------|------|
| I²C SDA / SCL (touch·light·PMU 공유) | 5 / 6 |
| SPI MISO / MOSI / SCK (TFT·SD 공유) | 8 / 17 / 18 |
| TFT CS / RST / DC / BL | 39 / 47 / 9 / 48 |
| Touch RST (CST226SE) | 13 |
| Light sensor IRQ (LTR-553) | 21 |
| SD CS | 14 |
| 버튼 Boot / IO12 / IO16 | 0 / 12 / 16 |

LovyanGFX 패널 설정:
- ST7796, SPI2_HOST, mode 0, write 40MHz / read 16MHz, 3-wire, DMA auto
- panel 222×480, memory 320×480, **offset_x=49**, offset_y=0
- `invert=true`, `rgb_order=false`(RGB), `bus_shared=true`
- 백라이트: pin 48, freq 44100, PWM ch 7, invert false

빌드 설정:
- `memory_type=qio_opi` (R8 = QIO flash + **OPI/octal** PSRAM — 틀리면 부팅 크래시)
- flash 16MB, partition `default_16MB.csv`
- extra flags: `ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`,
  `ARDUINO_RUNNING_CORE=1`, `ARDUINO_EVENT_RUNNING_CORE=1`, `BOARD_HAS_PSRAM`
