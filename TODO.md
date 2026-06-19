# TODO — 추후 구현 목록

> 지금까지 진행 상황은 [PROGRESS.md](PROGRESS.md) 참고. 이 문서는 **다음에 할 일**만 모음.
> (논의는 끝났고 구현만 남은 항목들)

## 1. 무동작 시 자동 디밍 → 자동 화면 끄기 — ✅ 코드 구현됨 (실기 확인만 남음)
> `main.cpp applyBacklight()`에 이미 구현: `DIM_MS=60s`→디밍(`DIM_LEVEL=153`),
> `OFF_MS=120s`→완전 끔. **대시보드·키패드(언락) 양쪽 적용**(루프 최상단 전역 호출).
> 깨우기: IO16/더블탭. 자동끔↔수동토글은 `gManualOff` 하나로 일원화. 남은 건 실기 확인뿐.
> (아래는 원래 설계 메모 — 참고용)
- 대시보드(메인)에서 **일정 시간 입력(터치/버튼) 없으면 백라이트 낮춤**,
  더 지나면 **완전히 끔**(`setBrightness(0)`, 실제 절전).
- **PIN 키패드(언락) 화면에도 동일 적용** 가능 — 사용자 요청.
- 깨우기: 아무 입력(IO16/Home/터치)으로 복귀. 이미 IO16 토글·Home 새로고침 로직 있음.
- 구현 메모: `main.cpp` 루프에 마지막 입력 시각(`gLastInput`) 추적 → 단계별 타임아웃
  (예: 30s→디밍 80, 60s→끔). 입력 감지 지점(`touch::read`, `io16Pressed`,
  `io12Pressed`, `homePressed`)에서 `gLastInput=millis()` 갱신.
- 주의: 자동 끔과 IO16 수동 토글(`gScreenOff`) 상태가 충돌하지 않게 일원화.

## 2. 폴링 실패 시 "이전 데이터 유지" (graceful degradation) — ✅ 완료
- 폴링 실패 시 마지막 정상 대시보드 유지 + 상단 amber 점(`gStale`, `main.cpp`).
  첫 실패(데이터 없음)에만 에러 화면. 타임아웃 15s + 1회 재시도(`Api.cpp`).

## 3. 터치 슬라이드로 PIN 입력 (손 안 떼고)
- 그리드 키패드라 부작용 있음(논의됨):
  - **(A) 순수 스와이프**: 지나가는 칸 모두 입력 → **일직선/인접 경로 PIN만 정확**(예: 2580 세로 일자). 다른 PIN은 중간 숫자 오입력 → 10회 실패 시 와이프 위험.
  - **(B) 멈춤(dwell) 입력**: 각 숫자에서 ~0.25s 멈추면 입력. 임의 PIN 가능하나 연속숫자/정밀도 이슈.
- 결정 필요: 사용자가 경로형 PIN 고정이면 (A), 아니면 (B) 또는 현행 탭 유지.

## 4. 둥근 폰트 (B안, 정통)
- 레퍼런스 Baloo 느낌. TTF → LovyanGFX 폰트(또는 AA 비트맵)로 임베드.
- 현재 % 숫자/타이틀은 FreeSansBold. 워드마크는 이미 AA RGB565로 처리함(참고 가능).

## 5. 사용량 히스토리 그래프 (스파크라인) — ✅ 완료
- IO12 순환에 히스토리 페이지 추가(대시보드→상세→히스토리). 5h/7d 스파크라인,
  64샘플 버퍼(`gH5/gH7`, `main.cpp`), `DisplayHAL::drawHistory`.
- (추후) 시간축 라벨/더 긴 보관/NVS 영속화 등은 여력 될 때.

## 6. 보안 하드닝 (별도 트랙)
- **TLS CA 핀닝**: 현재 `WiFiClientSecure::setInsecure()`(암호화O, 서버검증X)
  → api.anthropic.com 인증서 검증 추가로 MITM 차단 (`Api.cpp`).
- **ESP32 flash 암호화(eFuse)**: at-rest로 WiFi 비번까지 보호. 비가역이라 신중.

## 7. 배터리 정확도 튜닝 (장기 테스트 후)
- 현재: USB 중 100% 고정, 배터리 구동 시 0.25s×20 슬라이딩 중앙값(`Power.cpp`).
- 실사용 데이터로 전압→% 곡선(`CURVE[]`)·윈도우 길이 보정.
- (선택) 충전 중에도 대략 진행률 표시하려면 별도 로직.

## 8. 기타 아이디어 (낮은 우선순위)
- 셋업 포털에서 폴링 주기 등 설정 변경.
- 화면 페이지 추가(네트워크 상세 등) — IO12 순환 확장.

---

## 9. 친구 선물용 — 초기 설정 간소화 (다음 작업, 우선순위 높음)
> 목표: 비개발자 친구가 WiFi + Claude OAuth 토큰 설정을 막힘없이 하도록.
> 우리가 겪은 4대 마찰: ① 토큰 발급 헷갈림 ② 긴 토큰 폰 입력 ③ 2.4GHz 고르기 ④ 매부팅 PIN.

**추천 구현 순서: 9.1 → 9.2 → 9.3 → (선택)9.4**

> ⏳ **실기 E2E 미검증**: 9.1~9.3 모두 빌드/플래싱 완료. 폰으로 실제 셋업
> (QR 스캔→AP 접속 / 드롭다운 SSID / 틀린 비번·토큰 거부 / 캡티브 팝업 / 안내문)
> 테스트는 **추후 진행**(사용자가 결과 보고 9.4 진행여부 알려줄 예정).

### 9.1 토큰 즉시 검증 — ✅ 완료
- `handleSave`에서 provision 전에 **AP+STA 동시 모드**로 입력한 WiFi에 실제 접속
  (`net::apStaConnect`, 폰은 AP에 계속 연결 유지) → 연결 실패 시 즉시 안내.
- WiFi 연결되면 `api::poll(token)`로 토큰 검증: httpCode 200/400=유효, 401=무효,
  ≤0=네트워크/도달 불가. 실패 사유별 스타일된 결과 페이지 반환(`Portal.cpp`).
- 검증 통과 후에만 저장+리부트. 상수 `CUM_VERIFY_WIFI_TIMEOUT_MS`(12s).
- ⚠️ 트레이드오프: STA 접속 시 AP 채널이 라우터 채널로 이동 → 폰이 잠깐 끊겼다
  재접속(CSA). 대부분 견디나, 드물게 실패 결과 페이지를 못 받을 수 있음(성공 시엔
  어차피 리부트라 무관).

### 9.2 WiFi 스캔 드롭다운 + 캡티브 자동 팝업 — ✅ 완료
- 셋업 진입 시 `net::scanNetworks()`로 주변 네트워크 스캔(이 칩은 2.4GHz 전용이라
  목록 자체가 곧 호환 밴드) → 포털 폼에 `<select>` 주입(`portal::scanNetworks`,
  AP 올라오기 전 스캔). 드롭다운 선택이 SSID 입력칸을 채움(숨김/수동 입력도 가능).
- 캡티브 자동 팝업: setup 모드 `onNotFound`를 302 리다이렉트(`handleCaptive`)로 변경
  → OS 점검 URL이 폼으로 튕겨 AP 접속 시 설정창 자동 표시.

### 9.3 QR 코드 + 포털 토큰 안내문 — ✅ 완료
- 셋업 화면(`drawProvisioning`)에 **WiFi 조인 QR**(`WIFI:T:WPA;S:<ap>;P:<pw>;;`,
  `qrEscape`로 예약문자 처리, LovyanGFX `qrcode()` margin=true) → 폰 카메라로 AP 자동
  접속. 좌측엔 수동 접속 폴백(SSID/Pass) + 열 페이지 주소.
- 포털 폼(`setupHtml`)에 토큰 발급 **단계별 안내 + 함정 명시**(③ 코드 재붙여넣기 강조).

### 9.4 (선택) PIN 없는 자동 언락 모드 — 매일 마찰 제거
- 현재 매부팅 웹 언락이 친구에겐 번거로움. 옵션으로 **토큰을 기기 키로만 암호화** →
  부팅 시 자동 복호화(웹 언락 생략).
- 트레이드오프: 기기 분실+플래시 덤프 시 토큰 노출 가능(flash 암호화 미적용 시).
  데스크 가젯이면 보통 수용 가능. 셋업에서 "PIN 사용/미사용" 선택지로 둘 수도.
- 결정 필요(보안 취향). 구현 시 `CredentialStore`에 토큰을 device-key로 봉인하는 경로 추가.

### 9.5 동봉 Quick Start 카드 (코드 아님)
- 3단계 그림 설명서. 펌웨어는 미리 구워서 선물. (토큰은 친구 계정이라 대신 입력 불가.)

---

## 10. 터치 스크롤 민감도 개선
> 배경·현재상태·전체 메커니즘은 [SCROLL.md](SCROLL.md). 로직은 전부
> `main.cpp scrubUpdate()`. 현재 상수: `STEP=7, FIRST=3, GLITCH=70, GRACE=2`,
> 파이프라인=1프레임.

- [ ] **방향 전환 스냅** — 스와이프 부호가 바뀌면 `accum`을 0으로 스냅 + `moved=false`
  재무장 → 전환 첫 칸을 `FIRST`로 처리. 전환 지점의 "최대 한 칸 슬랙" 제거
  (SCROLL.md "Direction reversal" 참고). 저위험.
- [ ] **정지 고정(settle-lock)** — 손가락이 N프레임 정지하면 커서 고정, 명확한 재드래그
  전까지 드리프트 무시. "도착→멈춤→뗌"의 lift-off 튐을 시작지연 없이 제거. 정지 임계(≤1~2px)
  vs 느린 드래그 튜닝 주의.
- [ ] **입력 스무딩(저역통과)** — `filtY += (ty-filtY)*α`로 지터/스파이크 완화 → 글리치 캡
  덜 발동. 약간 지연.
- [ ] **비대칭 지연** — 첫 칸은 즉시 커밋(파이프라인 우회), 진행/릴리즈만 폐기 적용.
  시작 즉각 + lift-off 보호 동시. 코드 늘어남.
- [ ] **터치 드라이버/lift 감지** — CST226SE는 폴링(INT 미배선, POWER.md). 컨트롤러가
  마지막 보고에 lift 플래그를 준다면 실제 lift-off 프레임만 정밀 폐기 가능 → 파이프라인 단축/제거.

## 11. WiFi 연결 메뉴 (집 ↔ 회사 수동 전환)
> 목표: 네비 메뉴에 `WiFi`(또는 `Connect`) 항목 → 들어가면 **저장된 망 목록 + 현재 신호세기**
> 표시, 홈버튼으로 선택해 **수동 연결**. 장소 이동 시 자동 로밍을 기다리지 않고 강제 선택.

> ✅ **경량판 먼저 구현됨 (A+B)** — 장소 이동 시 바로 못 잡는 답답함 해소가 실제 목적이라,
> 망을 직접 고르는 풀 UI 대신 다음 둘만 적용:
> - **A. 메뉴 `Reconnect` 행** (`MENU_RECONNECT`) → 누르면 즉시 `roamReconnect()`로 전체
>   스캔 후 가장 강한 저장망에 연결("Connecting…" 스플래시). 이미 연결돼 있으면 no-op이라
>   동작 중 링크를 끊지 않음. 진행 중 poll은 ~3s 기다린 뒤 실행, 새 링크에서 즉시 폴.
>   (행 7개 수용 위해 `M_HDR_H 28→24`, `M_ROW_H 32→28`.)
> - **B. 자동 로밍 가속** — `CUM_REROAM_AFTER 3→1`: 첫 실패에서 바로 전체 스캔 로밍
>   (~30s → ~5s). 대가는 짧은 블립에도 2~4s 스캔(USB 상시라 수용).
>
> **남은 풀버전(아래)**: 망을 *직접 골라서* 붙는 목록 UI(신호바/(x)표시/커서 모달). 어느 망에
> 붙을지 손으로 고르고 싶을 때만 필요. 지금은 "거기 있는 망에 자동으로 잡기"로 충분해 보류.

### 이미 있는 것 (재사용)
- 저장 목록 RAM 캐시: `gSsids[]/gPws[]/gWifiN` (`main.cpp`, 부팅 시 로드). `CUM_WIFI_MAX=3`.
- `net::connectMulti(ssids, pw, n, preferredIdx)` — 특정 저장망 직접 연결(인덱스 지정 시
  무스캔 시도→실패 시 스캔 폴백). 수 초 블로킹.
- `net::scanNetworks(out, maxN)` — 전채널 스캔 ~2~4초 블로킹, 주변 SSID 반환 → 저장망과 매칭해 RSSI.
- `net::isConnected()/ssid()/rssi()`, `WiFi.RSSI()`. `storage::lastWifi()/setLastWifi()`.
- 로밍 폴백 `roamReconnect()`(선택망 부재 시 처리). UI 패턴: 설정 화면
  (`drawSettings` + `gSettingsOpen` 모달) 그대로 복제 — 스와이프/IO12·IO16/홈선택/Back행.

### 화면 구성
```
WiFi
  HomeNet      ||||   <- 저장 SSID + 신호바/ dBm; 현재 연결망 표시(체크/코랄)
  OfficeNet    ||
  CafeNet      (x)    <- 저장됐지만 현재 범위 밖
  Rescan
  Back
```
- 저장망당 1행(최대 3), 각 행에 신호 표시(`drawWifiBars` 재사용 또는 dBm 텍스트).
- **현재 연결망** 표시(체크/코랄). 저장됐지만 스캔에 없으면 "(x)"로 범위 밖 표시.
- `Rescan` 행 → 재스캔으로 신호 갱신. `Back` 행 → 메뉴 복귀.

### 동작
- 메뉴/설정과 동일: 스와이프 또는 IO12(다운)/IO16(업) 커서 이동(순환), **홈버튼으로 실행**.
- 망 행에서 홈 → **연결**: `connectMulti(gSsids, gPws, gWifiN, 인덱스)` 블로킹 →
  "Connecting…" 스플래시(`connectWithAnim`처럼 다른 코어 애니). 성공 시
  `setLastWifi(index)` + 연결표시, 실패(범위 밖) 시 "Not found/failed" 후 잔류.
- `Rescan` 홈 → "Scanning…" 후 신호 재표시. `Back` 홈 → 메뉴.

### 신호 갱신 방식 (결정 필요)
- **추천: 진입 시 1회 스캔 + 수동 Rescan**. 단순/저전력. 진입 시 ~2~4초 "Scanning…".
- 대안: 화면 열려있는 동안 주기 자동 재스캔 — 부드럽지만 전력·블로킹↑.

### 엣지/주의
- **스캔·연결 모두 블로킹** → "Scanning…/Connecting…" 표시 필수, 가능하면 다른 코어에서
  돌려 UI·절전 루프 안 굶게(`connectWithAnim`/부트애니 태스크 참고). 연결 중 폴 충돌 방지.
- **저장된 망 중 선택만** — 새 망 추가는 셋업 포털 영역(범위 밖).
- 수동 연결 후 새 링크로 폴 재개, 이후 위치변경은 자동 로밍 폴백이 커버.
- 평문 새로 저장 금지 — 비번은 이미 `gPws[]` RAM에 있음, `connectMulti`에 전달만.

### 구현 스케치
1. `MenuItem`에 `MENU_WIFI` 추가 + `kMenuRows`에 `{"WiFi", MENU_WIFI}`(위치는 Settings 근처).
2. 모달 상태 `gWifiOpen` + `gWifiCursor` (`gSettingsOpen` 미러).
3. `display::drawWifi(...)` — 타이틀/저장행+신호/Rescan/Back; `drawScrollbar`·커서카드·
   `drawWifiBars` 재사용.
4. 진입 스캔: `{ssid→rssi, present}` 캐시, 저장 SSID와 이름 매칭.
5. `menuActivate`의 `MENU_WIFI`가 WiFi 화면 열기(먼저 스캔).
6. WiFi 모달: `scrubUpdate`+IO12/IO16 커서; 홈 → 연결/재스캔/Back, 스플래시 표시.

### 결정할 것 (구현 전)
- 신호 갱신: **진입 1회 + Rescan**(추천) vs 주기 자동.
- 항목 이름: **`WiFi`** vs `Connect` (추천 `WiFi`).
- dBm 숫자 vs 바만 표시.

## 12. WiFi wake 반응속도 ↔ 배터리 트레이드오프
> 배경: 배터리 구동 시 화면 off(타이머/수동) 전환에서 `net::radioOff()`로 **라디오를
> 완전히 끔**(`WiFi.mode(WIFI_OFF)`). 깰 때는 `radioWake()`가 마지막 AP만 무스캔
> 재연결하지만, association부터 다시 하는 **cold reconnect**라 링크 올라오는 데 시간이
> 걸림. (USB일 때는 `if (!gOnUsb)`로 애초에 radioOff 안 함.)

### ⓑ 첫 poll 고정 대기 제거 — ✅ 완료 (이 커밋)
- 기존: `wakeShow()`가 깰 때 `gPollBackoffMs=2000`으로 **첫 poll을 2초 고정 지연**.
- 변경: `gWakePollPending` 플래그 추가 → 메인 루프에서 `net::isConnected()` 되는
  **즉시 poll**. 2초 backoff는 "링크가 끝내 안 붙는 경우(자리 이동)"의 fallback 상한
  겸 roam escalation 트리거로만 잔류. **배터리 손해 없이** 체감 지연만 단축.

### ⓐ cold reconnect 자체 제거 — 미구현 (트레이드오프, 결정 필요)
- 방법: 화면 off 시 `radioOff()` 대신 **모뎀슬립 유지**(`WiFi.setSleep(true)`) →
  깰 때 association이 살아 있어 거의 즉시 연결.
- 대가: 자는 내내 DTIM 주기(~100~300ms)마다 라디오가 비콘 수신 → **상시 소량 소모**.
- 유불리는 사용 패턴에 따라 갈림:
  - **오래 자고 가끔 깨움** → 현행(radioOff)이 유리 (reconnect 피크가 드묾).
  - **자주 들여다봄(짧은 off/on 반복)** → 모뎀슬립 유지가 빠르고 총소모도 적을 수 있음
    (reconnect 피크 누적 > baseline).
- 검증 난점: 전류계 없이 실측이 어려움(POWER.md 라이트슬립 단락과 동일 이슈).
- 결정 필요: 실사용 패턴 확인 후 진행. 옵션화(설정에서 "빠른 깨우기/절전" 토글)도 가능.
