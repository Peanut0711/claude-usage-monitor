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

## ✅ Stage 3 — 완전 종료 (E2E 실기 검증 완료)

Anthropic API 폴링 + rate-limit 헤더 파싱 + 대시보드까지 구현·검증 완료.
언락 후 `Running` 상태에서 60초 주기로 폴링, 사용률 바 2개 + 리셋 카운트다운 표시.

확정된 API 사실 (실제 응답으로 검증함):
- 요청: `POST https://api.anthropic.com/v1/messages`, 본문
  `{"model":"claude-haiku-4-5-20251001","max_tokens":1,"messages":[{"role":"user","content":"."}]}`
- 필수 헤더: `Authorization: Bearer <token>`, `anthropic-version: 2023-06-01`,
  `anthropic-beta: oauth-2025-04-20`, `User-Agent: claude-code/2.1.5` (OAuth 토큰엔 이 UA 필요).
- 응답 헤더: `anthropic-ratelimit-unified-{5h,7d}-{utilization,reset}`.
  - **utilization = 0~1 분수** (예: 0.2 = 20%) → ×100 해서 퍼센트 (`Api.cpp`).
  - **reset = 유닉스 epoch 초** → NTP(`time()`) 기준 카운트다운 (`main.cpp fmtCountdown`).
- TLS: `WiFiClientSecure::setInsecure()` (암호화O, 서버검증X). 추후 CA 핀닝 하드닝.

운영 메모(삽질로 확정):
- **`claude setup-token`의 진짜 흐름**: 실행 → 브라우저 허용 → 브라우저가 보여준 **코드
  (`xxxx#yyyy`)를 다시 터미널 프롬프트에 붙여넣어야** 진짜 토큰(`sk-ant-oat01-...`)이 출력됨.
  코드 자체를 토큰으로 쓰면 401 "Invalid bearer token". 반드시 별도 일반 터미널에서 실행.
- 토큰 검증 스크립트: `C:\Users\user\test_token.ps1` (HTTP 200 + util 헤더 확인용).
- **토큰 재입력(교체)**: 부팅 직후 "Starting…" 2.5초 창에서 **사이드 버튼 IO12/IO16** 누르면
  자격증명 와이프 → 셋업 모드 복귀 (`main.cpp factoryResetRequested`). BOOT는 외부 미노출.
- 수정한 버그: `sealAndStore` 버퍼가 WiFi 크기(97B) 기준이라 긴 토큰 봉인 실패 →
  `CUM_TOKEN_MAX_LEN`(1024) 기준으로 확대.

## ✅ Stage 4 (진행 중) — 테마 대시보드 (실기 확인)

레퍼런스 디자인대로 재미요소 추가, 실기 확인 완료:
- 상단: 코랄 **픽셀아트 마스코트**(`MASCOT[8]` 11×8 비트맵) + "Usage" + **WiFi 신호막대**
  (RSSI 실데이터) + **배터리 아이콘**(충전 `+`, 수치는 PMU 전까지 플레이스홀더 100%).
- 카드 2개: 큰 % + 보라 알약 배지 + **색상 다른 바**(Current=주황 `T_CUR`, Weekly=라임 `T_WK`)
  + "Resets in …" 카운트다운.
- 하단: 폴링마다 바뀌는 장난기 문구(`kStatus[]`: Divining/Counting tokens/…).
- 구현: `DisplayHAL::drawDashboard(const Dashboard&)`, 헬퍼 drawMascot/drawWifiBars/
  drawBattery/drawPill/drawMetricCard. 팔레트 `T_*` 상수.

### Stage 4 진행
1. ✅ **터치 PIN 키패드** — CST226SE 브링업(SensorLib `TouchDrvCSTXXX`, I²C 0x5A,
   RST=13, 폴링) → 온스크린 숫자패드로 PIN 입력. 웹 언락은 폴백으로 유지.
   터치 좌표 매핑: `src/input/Touch.cpp`의 `TOUCH_SWAP_XY/FLIP_X/FLIP_Y` (현재
   SWAP=1, FLIP_Y=1로 동작 확인). lib_deps에 `lewisxhe/SensorLib@^0.2.4`.
2. ✅ **더블버퍼링** — 모든 드로잉을 PSRAM `LGFX_Sprite canvas`에 그린 뒤 `pushSprite`
   한 번에 전송 → 티어링/깜빡임 제거. (`DisplayHAL.cpp`)
3. ✅ **즉시 새로고침** — Running 상태에서 사이드 버튼(IO12/IO16) 누르면 바로 폴링
   (`main.cpp sideButtonPressed`). 부팅 시 같은 버튼 길게 = 초기화(동작 분리).
4. ✅ **SY6970 PMU 배터리** — XPowersLib `PowersSY6970`(I²C 0x6A, 연속 ADC).
   전압→1S Li-ion 곡선으로 잔량 추정, `isCharging()||isVbusIn()`로 충전표시.
   배터리 없으면 USB 구동으로 보고 100%/충전. (`src/power/Power.*`)
   lib_deps에 `lewisxhe/XPowersLib@^0.2.6`(설치된 건 0.2.9).
5. 🔜 (선택) 화면 페이지 전환(상세/네트워크/히스토리), 자동 밝기(조도센서), 커스텀 폰트.

## ⏭️ 이후 단계

- **Stage 5(선택)**: LVGL 도입 (HAL 레이어 이미 분리 → `display::gfx()`로 flush 콜백 연결).
- **하드닝(별도)**: TLS CA 핀닝, ESP32 flash 암호화(eFuse)로 at-rest 강화.
- **커스텀 폰트(선택)**: 둥근 폰트(Baloo류)를 VLW로 임베드하면 레퍼런스에 더 근접.

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
