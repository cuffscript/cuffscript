# 예제 스크립트

번호 순서대로 훑어보면 언어 기능을 단계적으로 익힐 수 있습니다. 전부 실제로
실행되는 것을 확인했습니다 (`make && ./cuffc examples/0X_....cuff`).

| 파일 | 보여주는 기능 |
|---|---|
| `01_hello.cuff` | 가장 단순한 `print` |
| `02_comprehensive_demo.cuff` | 상수, 함수(`returnable`), `is`/`!` 결합, `async`+`await`+`or_else`, 1-Based 리스트, `loop repeat`+`stop`, `use DLC:`/`use ... from ...` — 명세 문서의 종합 검증 코드 그대로 |
| `03_pattern_matching.cuff` | `match/find/replace/split/count`, 이름 있는 캡처(`<name:...>`), 번호 캡처, `[one:...]`, `IS` 대소문자 무시, 이스케이프(`\.`) |
| `04_collections.cuff` | 리스트/맵 `add`/`change`/`remove`, 슬라이싱(`[2~4]`, 음수 인덱스), `or_else`로 인덱스 초과 복구 |
| `05_scoping_and_globals.cuff` | 함수 레벨 스코프, `change x to global` |
| `06_error_recovery.cuff` | `or_else`가 실패한 선언을 어떻게 복구하는지 |
| `07_functions_async.cuff` | 재귀 `returnable` 함수, `async`+`returnable` 조합 |
| `08_dlc_libraries.cuff` | `DLC:math`/`DLC:string`/`DLC:random` |
| `09_modules_demo.cuff` + `lib/greetings.cuff` | `use <이름> from <경로>`로 다른 `.cuff` 파일 불러오기 |
| `02_comprehensive_demo.cuff`가 참조하는 `maps/core_engine/stage_data.cuff` | 위와 같은 커스텀 모듈 로딩의 두 번째 예시 |

## error_cases/

일부러 실패하도록 만든 스크립트들 — 각 에러 종류가 실제로 어떤 메시지를 내는지
확인하는 용도입니다. (`or_else`로 감싸지 않았으므로 전부 0이 아닌 종료 코드로
끝나는 게 정상입니다.)

```bash
for f in examples/error_cases/*.cuff; do
    echo "--- $f ---"
    ./cuffc "$f"
done
```

## 직접 실행하기

```bash
make
./cuffc examples/01_hello.cuff
./cuffc --ast examples/01_hello.cuff   # 실행 대신 토큰/AST만 보기
```
