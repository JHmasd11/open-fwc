# Open Frame Window Counter (v0.1.2)

GD 2.2091 / Geode 6.0.0 / Windows x64, Android ARM32 and ARM64.

## 빌드
1. GitHub에 빈 repository를 생성합니다 (예: open-fwc).
2. 이 ZIP을 풀고 내부 파일 전체(CMakeLists.txt, mod.json, src, tests, .github 등)를 저장소 루트에 커밋합니다. .github/workflows/build.yml 누락 주의.
3. Actions > Build OpenFWC > Run workflow (push to main 시 자동 실행).
4. unit-test, Windows, Android32, Android64, package가 모두 성공하면 Artifacts의 OpenFWC-v0.1.2 ZIP을 받습니다.
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
- 입력은 변경하지 않습니다. 입력 리플레이, 프레임 윈도우 측정, CBF 지원은 이후 단계입니다.
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
