# Open Frame Window Counter (v0.3.1)

GD 2.2091 / Geode 6.0.0 / Windows x64, Android ARM32 and ARM64.

## 빌드
1. GitHub에 빈 repository를 생성합니다 (예: open-fwc).
2. 이 ZIP을 풀고 내부 파일 전체(CMakeLists.txt, mod.json, src, tests, .github 등)를 저장소 루트에 커밋합니다. .github/workflows/build.yml 누락 주의.
3. Actions > Build OpenFWC > Run workflow (push to main 시 자동 실행).
4. unit-test, Windows, Android32, Android64, package가 모두 성공하면 Artifacts의 OpenFWC-v0.3.1 ZIP을 받습니다.
5. ZIP 안의 .geode를 Geode의 mods 폴더에 복사하고 게임을 재시작합니다. SDK 및 로더 버전 6.0.0을 사용하세요.

## 테스트 절차
- 다른 입력/물리 관련 모드(CBF, TPS 변경, 매크로, 프레임 스텝)는 우선 끕니다.
- 일반 모드에서 짧은 로컬 테스트 레벨에 진입하여 점프를 3번 누르고 떼어 봅니다. HUD에 down/up 카운터와 마지막 progress가 갱신되어야 합니다.
- 길게 홀드했다 놓고, 죽거나 재시작하고, 레벨을 나옵니다. 이전 시도는 CSV로 저장되고 HUD는 새 시도에서 초기화되어야 합니다.
- Geode 로그의 [FWC] saved attempt 메시지에서 CSV 경로를 찾습니다. 위치는 Geode의 해당 모드 save directory (모드 ID: openfwc.frame-window-counter)입니다.
- Windows, Android 각각에서 CSV와 Geode 버전, FPS, 테스트 레벨 이름을 기록해 주세요. 오류가 있다면 GitHub Actions의 실패한 단계 로그 또는 Geode crash log를 공유하세요.

## 기록 형식과 제약
- `handleButton` 진입 직전의 값이 아니라, 원래 게임 함수 호출 전 hook에서 관찰한 값. 게임이 입력을 최종 적용한 타이밍이라는 보장은 아직 없음.
- `progress_raw`, `tick_index_raw`, `command_index_raw`는 게임 내부 필드 원값입니다. 실제 물리 틱으로 확정하기 전 비교 검증이 필요합니다.
- `logic_call`은 processCommands 호출 횟수이며, 물리 틱과 동일하다고 가정하지 않습니다. half/last flags는 해당 함수 안에서 발생한 입력에만 의미가 있습니다.
- button은 게임 내부 번호이며 P1/P2는 handleButton 인수 기준입니다. 듀얼 공유 입력이나 자동 입력은 실제 플레이와 비교 검증해야 합니다.
- CSV는 재시작, 완료, 레벨 종료 시 시도별로 저장합니다. 입력 없는 시도는 파일이 생성되지 않습니다. 비정상 종료 시 현재 시도는 잃을 수 있고, 파일 저장 실패는 로그에 표시됩니다.
- HUD와 기록은 일반 PlayLayer 대상으로 개발했습니다. 에디터 내 플레이테스트는 지원 대상이 아닙니다. 연습 모드의 체크포인트 복원 타이밍은 아직 검증하지 않았습니다.
- 일반 기록 모드에서는 입력을 변경하지 않습니다. 실험용 리플레이는 명시적으로 설정에서 활성화합니다. 프레임 윈도우 측정 및 CBF 지원은 이후 단계입니다.
- v0.1.0은 사용자 환경에서 컴파일과 CSV 기록을 확인했습니다. v0.1.1의 SDK 컴파일 및 실제 실행은 아직 검증하지 않았습니다.

## 개발 참고
- https://github.com/geode-sdk/geode/releases/tag/v6.0.0
- https://github.com/geode-sdk/bindings/tree/main/bindings/2.2091
- https://github.com/geode-sdk/build-geode-mod

## v0.1.1 변경 사항
- 초기 UP 및 중복 입력은 P1/P2/버튼별 상태 추적으로 필터링합니다. 실제 게임 입력은 변경하지 않습니다. 시작 시 이미 눌린 버튼의 상태 및 연습 모드 복원은 미검증입니다.
- resetLevelFromStart와 resetLevel을 중첩 보호하며, 진행 카운터 역행을 보조 감지합니다. ESC 재시작 실기기 검증이 필요합니다.
- CSV에 tick_240_candidate(-1=불확실), level_id, level_name, end_reason을 추가합니다. 후보 값은 수집된 일반 모드 데이터에서만 검증된 관계를 적용합니다.
- Windows runner는 windows-2022로 고정합니다.
- 파일 저장 위치는 기존과 동일합니다. 입력 없는 시도는 CSV가 생성되지 않습니다.

## v0.1.2 변경 사항
- pauseGame, resume, resumeAndRestart, onQuit의 명시적 hook과 lifecycle 로그 추가. 일시정지 중 onExit는 기록을 제거하지 않습니다.
- 재시작 중첩 보호를 resumeAndRestart까지 확장했습니다. 지연 재시작의 실제 호출 순서는 검증 필요.
- 재개 시 실제 m_holdingButtons와 비교하여 누락된 release를 pause-sync로 기록합니다. pause-hook/pause-sync는 tick 후보 -1로 표시합니다. 이 이벤트의 정확한 물리 적용 시점은 확인되지 않았으므로 향후 매크로 재생에서 별도 처리해야 합니다.
- CSV에 event_source 열 추가. 기존 열은 유지합니다.
- 사용자가 v0.1.1 빌드 성공 및 초기 UP 수정 확인, ESC Restart/Resume 실패를 보고했습니다. v0.1.2의 Geode 빌드와 실기기 동작은 아직 미검증입니다.
- 테스트: 점프 2회 후 ESC → Resume → 점프 2회, ESC → Restart → 점프 2회, 점프 홀드 중 ESC → Resume → 추가 점프. 실패 시 [FWC] lifecycle, filtered, reconcile 메시지와 CSV를 공유해 주세요.

## v0.2.0 실험용 Macro Replay
- 사용자 테스트에서 v0.1.2의 ESC Resume/Restart 및 홀드 일시정지가 통과했습니다.
- Geode 설정에서 `Replay on pause restart (test)`를 켜세요 (기본 OFF). `Record inputs`도 ON.
- Create > My Levels에서 길고 평평한 로컬 일반 모드 테스트 레벨을 만든 후, 에디터 내부가 아닌 레벨 정보 화면에서 Play로 진입하세요. Start Pos, Practice, CBF, Click Between Steps, TPS 및 다른 매크로 모드를 끄세요.
- 점프 3회 이상을 각각 누르고 완전히 뗀 후 죽거나 클리어하기 전에 ESC > Restart. 아무 입력 없이 HUD를 관찰하세요. 원본 시도의 이벤트를 메모리에 복사하여 새 시도에서 한 번만 재생합니다. 사망/종료/일시정지는 재생 중단 또는 종료를 일으킵니다. 재생이 끝나면 일반 입력이 다시 허용되며 다음 재시작부터 다시 녹화합니다.
- Replay에서는 원본 `processQueuedButtons` 진입 시점과 실제 입력 시점의 내부 카운터가 일치하는 경우에만 이벤트를 주입합니다. 실제 엔진 함수 내부의 순서는 미검증입니다. `inside_queue`가 0인 이벤트, 반쪽 틱, 불완전 홀드, pause-sync 등은 거절합니다.
- `replay-...-done.csv` / `replay-...-stopped.csv`는 기존 save directory에 저장됩니다. 각 입력 직전 좌표의 오차 및 틱을 비교합니다. `inputs matched`는 전체 입력 주입 및 관측 좌표 오차 0.1 GD 좌표 단위 이하를 의미하며, 전체 레벨의 동일한 결과/클리어를 보장하지 않습니다. `position diverged`는 입력은 모두 주입했으나 좌표가 다름을 의미합니다.
- 실패하면 원본 fwc CSV, replay CSV (0개 주입이면 헤더만 있을 수 있음), [FWC] 로그를 보내세요. 자동 리플레이는 로컬 레벨만 지원하며 검증/기록 제출용이 아닙니다. 기존 CSV import, 완료 시도 재생, 재생 저장/불러오기, 체크포인트, Frame Window 계산은 미구현입니다.
- Geode SDK 전체 빌드 및 런타임 재현은 미검증이며, 순수 C++ 테스트만 실행했습니다.

## v0.2.1 — Replay input dispatch fix
- v0.2.0 사용자 실기기: 8/8 처리, 최대 위치 오차 61.7652, 점프 없음. 재생 중 입력 차단 hook이 모드 자체 주입도 차단할 가능성을 확인했습니다.
- 명시적으로 주입 중인 입력만 handleButton의 원래 게임 함수로 전달합니다. 일반 사용자 입력 차단은 유지합니다.
- 실제 PlayerObject의 m_holdingButtons를 호출 전후 비교합니다. 예상 상태 전환이 확인된 경우에만 재생 카운터 증가. 실패하면 REPLAY STOP / input not applied로 종료하며 실패 행도 CSV에 저장합니다.
- CSV에 held_before, held_after, input_verified 열을 추가합니다. 원래 입력 직전 위치 샘플은 유지합니다. 좌표 오차가 0.1을 초과하면 position diverged로 실패 처리합니다.
- 테스트는 로컬 일반 큐브 레벨에서 진행합니다. 각 점프 사이에 착지할 시간, 마지막 입력 해제 후 ESC Restart. 실패할 경우 원본 fwc CSV, replay CSV, [FWC] dispatch 로그를 공유하세요.
- 버튼 상태 전환 확인은 점프/전체 물리 상태 일치를 보장하지 않습니다. Geode SDK 빌드 및 실제 게임 테스트는 아직 미검증입니다.


## v0.3.1 — Native checkpoint restore probe (experimental)

- 기존 일반 모드 Recorder 및 Replay Engine 유지. 별도 opt-in 설정 `Checkpoint restore probe (test)`를 켜고 레벨에 다시 진입하면 동작합니다.
- 로컬 클래식 레벨의 Practice Mode에서 GD 자체 createCheckpoint/markCheckpoint/loadFromCheckpoint 호출을 관찰합니다. 메모리를 강제 복사하거나 물리값을 변경하지 않습니다. 수동 복원 자체는 게임의 기존 사망 후 Practice 체크포인트 재시작을 사용합니다.
- 자동 체크포인트를 끄고, 평평한 바닥을 달리다가 체크포인트를 하나만 놓습니다. 그 이후 입력하지 않습니다. 충분히 긴 평지 끝에 가시 하나를 배치해 `CP | READY` 이후 자연스럽게 사망하도록 하세요.
- 최초 120개의 non-half processCommands 호출 진입에서 원시 진행/틱/명령 카운터, levelTime, P1 X/Y, Y 속도, 이동 속도 및 상태 플래그를 저장합니다. 동일 체크포인트 복원 후 동일 진행 카운터에 정렬하고 120개 샘플을 비교합니다. 실제 경과 시간은 호출 간격에 따라 달라질 수 있습니다.
- PASS 조건: 모든 120개 진행/명령 카운터 및 상태 플래그 일치, 원시 tick 카운터는 첫 정렬 샘플에서 계산한 복원별 오프셋이 120개 샘플에서 일정해야 함, 시간 오차 <= 1e-6 s, 위치 오차 <= 0.001, Y 속도와 이동 속도 오차 <= 1e-5. 불일치/정렬 실패/입력 개입/샘플 구간 사망은 실패로 분류됩니다.
- HUD `CP | MATCH 120/120` 또는 `CP | FAIL`을 확인합니다. 모드 저장 폴더에서 `checkpoint-...-baseline.csv`, `checkpoint-...-matched.csv` 또는 `checkpoint-...-failed.csv` 및 [FWC] checkpoint 로그를 확인할 수 있습니다. baseline에는 actual 열이 비어 있습니다.
- 상태 플래그 bit 0~9: grounded, upside-down, ship, bird, ball, dart, robot, spider, swing, going-left.
- 제한: 첫 검증은 무입력 P1 단독 구간, CBF/TPS 변경/듀얼/StartPos 제외. Native 체크포인트의 전체 물리 상태, 트리거·RNG, 입력 큐 및 홀드 복원은 검증하지 않았으며 아직 자동 저장/임의 복원/Frame Window 분석을 제공하지 않습니다. 일반 모드 재생의 물리 타이밍 검증과 Practice 체크포인트의 검증은 별개입니다.
- 기존 Recorder/Replay 및 새 CheckProbe 순수 C++ 단위 테스트는 로컬에서 통과했습니다. Geode SDK 컴파일 및 실제 게임 실행은 GitHub Actions 및 사용자 환경에서 검증해야 합니다.

### v0.3.1 — Cumulative tick alignment

- 사용자 실측 2회에서 tick 오프셋 +3214/+6428은 일정했고 다른 기록 값은 정확히 일치했습니다. 절대 tick 값 대신 상대 증가량을 검증합니다. Progress, Command, 시간, 물리 검증 기준은 변경하지 않았습니다.
- CSV에 `tick_offset`, `reference_tick_offset`, `relative_tick_error`를 추가하고 원시 값도 유지합니다. 상대 오차가 0이 아니면 실패합니다.
- 전체 물리 상태 일치나 다른 레벨에서의 정확성을 보장하지 않습니다.
