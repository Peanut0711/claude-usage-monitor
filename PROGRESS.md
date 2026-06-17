# 진행 상황 / 다음 작업 핸드오프

> 마지막 업데이트: 2026-06-17. 다음 세션에서 이 문서부터 읽고 이어서 진행하면 됩니다.

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

**카드 레이아웃 현재 값 (`drawMetricCard`, 2026-06-17 튜닝 완료):**
| 요소 | 값 |
|------|----|
| 카드 | `cw=456 × ch=82`, 2장 y=34 / y=118 |
| % 숫자 | `FreeSansBold18pt7b` (히어로, 카운트업 주인공) — `yc+8` |
| 바 | `bh=12, r=6`, 너비 `cw-36` — `yc+42` |
| Resets | `FreeSans9pt7b` — `yc+62` |
> 이력: 처음 %=18pt·바=17px(r8)·Resets=12pt 였음 → "공간대비 큰 느낌" 피드백으로
> 바 12px·Resets 9pt로 축소, %는 18pt 유지(A안), 작아진 Resets 만큼 바/Resets를
> 아래로 내려 %-바 간격 확보(내부 간격 ~8px 균등). 폰트는 9/12/18pt 단위만 가능.

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
5. ✅ **드래그 밝기 조절** — 대시보드에서 좌/우 가장자리 스트립(x<48 또는 x>432)을
   위아래 드래그 → 밝기(위=밝게). `main.cpp` Running 케이스. (조도 자동밝기는 선호에
   따라 제거함 — Light 모듈 삭제.)
6. ✅ **키패드 폭 축소 + PIN 점 중앙정렬** — 가운데 96×42 셀 클러스터.
7. ✅ **백라이트 on/off 토글** — **전면 동그라미 = CST226SE Home 버튼**
   (`setHomeButtonCallback`)으로 처리. 한 번 누름=한 번 토글(이벤트 끊김>300ms로
   release 판정해 re-arm). 꺼지면 Running 루프 작업 skip, Home 재누름으로 깨움.
8. ✅ **사용률 카운트업 애니메이션** — 폴 결과가 바뀌면 바/% 가 RPG EXP처럼 차오르고
   도착 시 살짝 "푱". 상세·튜닝·변경 이력은 아래 **[카운트업 애니메이션]** 섹션 참조.

### 카운트업 애니메이션 (튜닝 & 변경 이력 — 수정 시 여기서부터 대화)

> 세밀한 수정이 계속될 영역. **수정할 때마다 아래 "변경 이력" 표에 한 줄 추가**하고,
> "현재 파라미터" 값을 갱신할 것. 이 섹션이 수정 논의의 단일 기준점.

**개념**: 새 폴 결과 도착 시 즉시 갈아끼우지 않고, 화면에 떠 있던 값(`gShownCur/gShownWk`)
에서 새 값까지 **1%씩 똑똑 올라감**(정수 스텝, RPG EXP 카운터 느낌, **오버슛 없음/선형**)
→ 도착하면 올라간 카드에만 **작은 스파크 + % 흰빛 플래시**. 값이 내려가면(리셋) 조용히
1%씩 카운트다운(스파크 없음). 전부 **논블로킹**: 메인 루프가 한 프레임씩 그림.
**길이는 변화량에 비례**(`steps×STEP_MS`): 작은 변화는 짧게, 큰 점프(첫 폴 0→N)는 길게
하되 `MAX_MS`로 상한. 두 카드는 더 큰 변화 기준으로 **동시에 착지**.

**관련 파일 / 심볼**:
- `src/main.cpp`
  - 상태: `gShownCur/gShownWk`(현재 표시값), `gStartCur/gStartWk`(시작값),
    `gTgtCur/gTgtWk`(목표값), `gAnimating`, `gPopCur/gPopWk`(증가한 카드만 pop), `gAnimStart`.
  - 상수: `STEP_MS`(1%당 시간), `MAX_MS`(큰 점프 상한), `POP_MS`(착지 후 페이드).
    런타임: `gAnimDur`(이번 climb 길이 = steps×STEP_MS, 상한 MAX_MS).
  - 함수: `startCountUp(cur,wk)`(시작 + `gAnimDur` 계산),
    `stepCountUp()`(한 프레임 전진+그리기, 정수 스냅·선형, 끝나면 `gAnimating=false`),
    `finishCountUp()`(목표로 즉시 스냅 — 페이지 이탈 시), `drawDashFrame(curPop,wkPop)`.
  - 구동 위치: Running 루프 하단 프레임 페이싱 블록 — `gAnimating && PAGE_DASH`일 때
    `stepCountUp(); delay(8);` 우선. 폴 소비부에서 `startCountUp` 호출(다른 페이지면 스냅).
- `src/display/DisplayHAL.cpp`
  - `drawMetricCard(..., float pop=0)`: pop>0이면 % 흰빛 플래시 + 바 흰빛 블렌드 + 스파크.
  - `drawSparks(x,y,pop,color)`: 바 끝 leading edge에서 점 6개가 퍼지며 페이드.
  - `lerpColor(a,b,t)`: 색 보간 헬퍼.
  - `Dashboard` 구조체(`DisplayHAL.h`)에 `curPop`, `wkPop`(0..1) 필드.

**현재 파라미터 (v3, 2026-06-17)**:
| 항목 | 값 | 위치 |
|------|----|----|
| 1%당 시간 | `STEP_MS = 100` ms (초당 ~10틱) | main.cpp |
| 큰 점프 상한 | `MAX_MS = 3000` ms | main.cpp |
| 착지 pop 페이드 | `POP_MS = 600` ms | main.cpp |
| 이징 | **선형(오버슛 없음)**, 정수 1% 스냅 | stepCountUp |
| 예) +9% 변화 | 9×100 = 0.9s + pop 0.6s ≈ 1.5s | — |
| 프레임 간 delay | `delay(8)` (카운트업 중) | main.cpp 루프 |
| pop 발동 조건 | 목표 > 시작 + 0.5% (증가한 카드만) | startCountUp |
| 스파크 개수 | 6 | drawSparks `dx/dy[]` |
| 스파크 비행거리 | `(1-pop)*15 + 2` px | drawSparks |
| 스파크 반경 | `3*pop + 1` px | drawSparks |
| pop 색 | **파스텔** = `lerpColor(barColor, 흰색, 0.6)` (카드색+흰색) | drawMetricCard |
| % 플래시 | T_TITLE→pastel `pop` | drawMetricCard |
| 바 플래시 | barColor→pastel `pop` | drawMetricCard |
| 스파크 색 | barColor→pastel `pop` (착지때 파스텔, 페이드때 원색) | drawSparks |

**변경 이력**:
| 날짜 | 변경 | 사유 |
|------|------|------|
| 2026-06-17 | v1 최초 구현(이징+오버슛+착지 플래시+스파크6, 논블로킹). 빌드+COM6 플래싱 완료. | 사용자 요청: RPG EXP식 성장감/도파민, "작은 스파크" 범위 |
| 2026-06-17 | v2: 총 길이 ~1.07s→**1.5s** (`ANIM_MS` 750→1200, `POP_MS` 320→300). | 1초는 너무 짧다는 피드백 |
| 2026-06-17 | v3: **오버슛 제거**(선형) + **1%씩 정수 스텝** 방식으로 전환. 고정시간 `ANIM_MS` 폐기 → `STEP_MS=180`/% (변화량 비례, `MAX_MS=3000` 상한). | 오버슛이 보기 불편(11~12 갔다 복귀), 변화 작으니 1% 틱이 재밌겠다는 의견. "성장 애니에 올인" |
| 2026-06-17 | v4: 착지 푱 페이드 `POP_MS` 300→**600ms**. | 300ms는 너무 짧다는 피드백 |
| 2026-06-17 | v5: pop 색을 순백색→**카드색 파스텔**(barColor+흰색 0.6)로. %·바·스파크 전부 적용. | 순백색은 허접; 상단 주황/하단 형광 톤에 맞춘 파스텔 요청 |
| 2026-06-17 | v6: 틱 속도 `STEP_MS` 180→**100ms**(초당 ~10틱). | 180ms는 "왜 안올라가지?" 싶게 느림 |

### 하드웨어 메모 (확정)
- **터치는 I²C**(CST226SE, SDA5/SCL6, 0x5A, RST13). 핀맵 이미지의 "TOUCH=SPI"는 오류.
- **전면 동그라미는 Home 터치버튼** — 좌표 아님. SensorLib `setHomeButtonCallback`
  이벤트(누르면 getPoint 중 콜백 발생). 백라이트 토글에 사용.
- **백라이트 LED는 픽셀 내용과 무관** → 검은 화면도 전력 소비. 절전은 밝기 0.
- 터치 좌표 진단: `CUM_TOUCH_TEST=1`로 부팅하면 raw/map 좌표 표시 화면.

### Stage 4 남은 선택지
- 둥근 폰트(B 정통: Baloo TTF→폰트헤더 임베드), 화면 페이지 전환(상세/네트워크/히스토리).

## ✅ 선물용 온보딩 9.1 / 9.2 — 구현·빌드·플래싱 완료 (2026-06-17)

TODO.md 섹션 9(친구 선물용 초기 설정 간소화) 중 9.1·9.2 완료. 상세는 TODO.md 참조.
- **9.1 토큰 즉시 검증**: `handleSave`에서 저장 전 **AP+STA 동시모드**로 입력 WiFi 실접속
  (`net::apStaConnect`, 폰은 AP 유지) → `api::poll`로 토큰 검증(200/400=OK, 401=무효,
  ≤0=도달불가). 실패 사유별 스타일 결과 페이지. 통과해야 provision+리부트.
  상수 `CUM_VERIFY_WIFI_TIMEOUT_MS`(12s). ⚠️ STA 접속 시 AP 채널 이동(CSA)으로 폰이
  잠깐 끊겼다 재접속 — 드물게 실패 결과 페이지 누락 가능(성공 시 어차피 리부트).
- **9.2 스캔 드롭다운 + 캡티브 팝업**: `net::scanNetworks()`로 주변 2.4GHz 목록을
  셋업 폼 `<select>`에 주입(`portal::scanNetworks`, AP 올리기 전 스캔, `enterSetup`).
  드롭다운 선택이 SSID 입력칸 채움(숨김/수동도 가능). setup `onNotFound`→302
  리다이렉트(`handleCaptive`)로 OS 캡티브 창 자동 팝업.
- 파일: `src/net/{Net,Portal}.*`, `src/config.h`. **남은 일**: 실기 E2E(폰으로 AP 접속 →
  드롭다운/토큰 검증/캡티브 팝업 동작 확인). 9.3(QR+안내문)·9.4(PIN리스)는 미구현.

## ✅ IO12 화면 깨우기 추가 — 빌드·플래싱·실기 확인 완료 (2026-06-17)

화면(백라이트)이 꺼진 상태에서 **IO12로도 깨우기** 가능하도록 추가. 기존엔 더블탭/IO16만
깨웠고 IO12 누름은 무시됐음(`io12Pressed()`로 엣지만 소비).
- `main.cpp` Running asleep 블록: `doubleTapDetected() || io12Pressed()`면 깨움.
  깨우는 누름은 이 블록에서 소비되므로 **페이지는 안 넘어감** — 떼었다 다시 눌러야
  다음 페이지로 순환(엣지 검출 특성). 더블탭·IO16 깨우기는 그대로 유지.
- `main.cpp:77~78` 오래된 "IO12 intentionally unassigned" 주석을 현재 동작에 맞게 수정.
- 검증: `pio run -e tdisplay-s3-pro` SUCCESS(경고 0), COM5 업로드(해시 검증 OK),
  실기에서 깨우기/페이지유지/재누름 순환 동작 확인 완료.

## ✅ 대시보드 카운트업 애니메이션 개편 — 빌드·플래싱·실기 확인 완료 (2026-06-17)

게이지 바 성장 애니메이션을 다듬음. 모두 `src/main.cpp`의 `startCountUp`/`stepCountUp`.
- **바 픽셀 연속화**: climb 값에서 `floorf` 제거. 큰 % 텍스트는 그릴 때 어차피
  정수 반올림(`DisplayHAL.cpp:310`)이라 **숫자는 1%씩 틱(~10Hz) 유지**되고, 바만 연속값을
  받아 **픽셀 단위로 부드럽게** 미끄러짐(기존엔 100ms마다 ~4px 계단).
- **수동 vs 주기 분기**: `startCountUp(cur, wk, fromZero)`. `fromZero=gPollAnimate`
  (Home=true/주기=false). **수동(Home)=0%부터 풀 리플레이**(값 같아도 재생),
  **주기(1분)=성장분만**(25→26%면 1%만 자라고 팡). 시작값은 각각 0/직전 표시값.
  변화 없으면(`>start+0.5` 거짓) 무애니·무팡.
- **카드 순차 진행**: 위(Current) 먼저 climb→팡, `GAP_MS`(250ms) 여유 후 아래(Weekly)
  climb→팡. 위 진행 중 아래는 시작값에 정지. 카드별 타임라인 `gDurCur`/`gDurWk`
  (기존 단일 `gAnimDur` 대체), `climbMs()` 헬퍼.
- 튜닝 노브: `STEP_MS`(100, 1%당 시간) `MAX_MS`(3000, 카드당 climb 캡) `POP_MS`(600)
  `GAP_MS`(250). ⚠️ 수동 풀 리플레이는 순차+여유로 최악 ~7.5s까지 길어질 수 있음
  (둘 다 큰 값일 때) — 길면 `MAX_MS` 축소.
- 검증: 빌드 SUCCESS(경고 0), COM5 업로드, 실기에서 순서/여유/바 부드러움 확인 완료.

## ✅ PIN 선택제 (기본 PIN 없음) — 빌드·플래싱·실기 확인 완료 (2026-06-17)

섹션 9 선물 간소화: 셋업에서 **PIN을 비우면 토큰을 기기키로 봉인**해 잠금화면을 생략.
보안 원하면 4자리 PIN 입력 시 기존처럼 PIN키 봉인. (at-rest를 진지하게 원하면 PIN이
아니라 flash 암호화로 가야 함 — PIN은 flash 덤프 앞에선 4자리라 한계. 그래서 B(eFuse)
대신 A(선택제)를 택함.)
- `provision(ssid, pass, token, pin)`: pin 빈 문자열이면 `deriveDeviceKey`, 4자리면
  `derivePinKey`. NVS 플래그 `CUM_NVS_TOKEN_PINNED`(`tokpin`)로 봉인 방식 기록.
- `credentials::tokenNeedsPin()` / `loadToken()`(기기키 복호화) 추가. `Storage`에
  `tokenPinned()`/`setTokenPinned()`.
- `main.cpp` 부팅: `tokenNeedsPin()`이면 `enterUnlock()`, 아니면 `loadToken()` 후 바로
  `enterRunning()`(키패드 생략). loadToken 실패 시 `enterSetup()`.
- `Portal.cpp`: PIN 검증을 선택형(빈칸 허용)으로, 셋업 폼 문구를 "비우면 잠금화면 생략"
  으로. `/unlock` 웹 페이지·wipe 정책은 PIN 사용자용으로 유지.
- ⚠️ **마이그레이션**: 기존 PIN-봉인 토큰은 새 펌웨어가 기기키로 못 풀어 셋업으로 떨어짐
  → **토큰 재입력 필요**(빈칸이면 옛 토큰 유지돼 재부팅 루프). loadToken 실패 시 `wipe()`로
  깨끗이 비워 함정 차단하는 건 **미적용 옵션**으로 남김(사용자 보류).
- 검증: 빌드 SUCCESS, COM5 업로드, 실기에서 무-PIN 셋업→재부팅 시 키패드 없이 대시보드
  진입 확인.

## ✅ 부팅 리셋 안내 개편 (안 3: 맥락 노출) — 빌드·플래싱·실기 확인 완료 (2026-06-17)

기존 `"Starting / Hold IO16 to reset"` 텍스트 화면 제거. 평소엔 스플래시만, **IO16을
실제로 누를 때만** 리셋 진행바 노출.
- `display::drawResetHold(frac)` 추가: 스플래시(로고+워드마크) + 위 힌트 "keep holding
  to reset" + 아래 채움 바.
- `factoryResetRequested()`: `drawSplash()`만 표시 → IO16 누르면 진행바, `HOLD_MS`(1500ms)
  채우면 리셋, 도중 떼면 스플래시 복귀.
- **위치 튐 수정**: 인트로 슬라이드인은 `yoff=28`(세로 중앙)에서 정지하는데 리셋 스플래시는
  `yoff=0`이라 28px 튀던 문제 → `SPLASH_REST_Y=28` 상수로 인트로 루프·정지 스플래시·
  리셋 홀드 위치 통일.
- 검증: 빌드 SUCCESS, COM5 업로드, 실기에서 위치 연속성/1.5s 채움/힌트·바 비겹침/중도복귀
  모두 확인 완료.

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
