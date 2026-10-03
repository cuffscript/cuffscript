# 구현 노트 (Implementation Notes)

`docs/SPEC.md`와 `docs/REGEX.md`는 언어의 문법과 의미를 대부분 정의하지만, 일부 지점은
의도적으로(또는 실수로) 명세되어 있지 않습니다. 이 문서는 실제 인터프리터
(`engine/interpreter/`)가 그런 지점들을 어떻게 구현했는지, 그리고 왜 그렇게
결정했는지를 기록합니다. 명세와 실제 동작이 다르게 읽힐 수 있는 부분은 전부
여기 있어야 합니다 — 새로 발견되면 이 문서에 추가해 주세요.

## 1. `async` / `await` 실행 모델

`SPEC.md`는 "비동기 함수의 실제 실행 모델과 세부 동작은 별도의 구현 명세에서
정의합니다"라며 명시적으로 결정을 미룹니다. 이 저장소에는 그 별도 명세가 없으므로,
이 엔진은 다음과 같이 구현합니다 (v0.2.0에서 개선됨 — 아래 "변경 이력" 참고).

- `async` 함수를 **`await`로 호출**하면, 이벤트 루프나 스레드 없이 그 자리에서
  동기적으로 끝까지 실행되고 값을 즉시 돌려받습니다.
- `async` 함수를 **`await` 없이 호출**하면 지금 실행되지 않습니다. 호출은 내부 큐에
  들어가고, **최상위 스크립트의 동기 코드가 전부 끝난 뒤** 큐에 들어간 순서대로(FIFO)
  실행됩니다. 큐에 들어간 호출의 반환값은 관찰할 수 없습니다 (애초에 `await`를 안 썼으니
  값을 받을 방법이 없음) — 항상 `empty`가 즉시 반환됩니다.
    - 큐를 비우는 시점은 스크립트 전체 실행 종료 시 딱 한 번입니다. 도중에(예: 각 최상위
      문장 끝마다) 비우는 방식도 고려했지만, 그러면 "큐에 넣은 바로 다음 줄이 실행되기도
      전에 비동기 작업이 끝나버려서" 사실상 즉시 실행과 체감상 차이가 없었습니다. "동기
      코드가 전부 끝난 뒤에 실행된다"는 규칙이 훨씬 이해하기 쉽고 실제로 "나중에
      실행된다"는 걸 보여줍니다. (`examples/10_async_ordering.cuff` 참고)
    - 실제 스레드/병렬성은 전혀 쓰지 않습니다 — 인터프리터의 공유 상태
      (`Environment`, 함수 테이블, 정규식 캐시 등)가 스레드 안전하지 않으므로, 진짜
      동시성을 도입하려면 그 전부를 뮤텍스로 감싸야 하는 큰 작업이 됩니다. 안정성을
      우선해 "협조적 지연 실행"만 구현했습니다.
- `await`를 `async`가 아닌 함수에 사용하면 `AwaitOnNonAsync` 런타임 오류가 발생합니다 —
  이렇게 하면 `await`가 여전히 "이 함수는 비동기다"라는 문서 역할을 합니다.
- `async`와 `returnable`은 서로 다른 수식어라서 함께 쓸 수 있습니다
  (`set async returnable func ...`). 명세 예제에는 등장하지 않지만 문법상 자연스러운
  조합이라 허용했습니다.

## 2. 정규식에서 `.`(마침표)의 의미

`docs/REGEX.md`의 이스케이프 대상 목록에는 `.`이 포함되어 있어서, 마치 `.`이 특수
의미(다른 정규식 언어처럼 "임의의 한 문자")를 가지는 것처럼 읽힐 수 있습니다. 하지만
CuffScript는 이미 그 역할을 하는 `[any]` 토큰을 명시적으로 제공하고, "복잡한 특수기호를
완전히 소멸시켰다"는 것이 언어의 핵심 설계 철학입니다. 따라서:

- **`.`은 기본적으로 리터럴 문자**입니다 (다른 문자와 동일하게 취급됩니다). 이메일,
  파일명, 버전 문자열처럼 마침표가 흔히 등장하는 일반 문자열을 `is`로 비교할 때 예상치
  못하게 동작이 바뀌는 것을 막기 위한 선택입니다.
- `\.`도 똑같이 리터럴 마침표를 만듭니다 (허용되지만 필수는 아님).

## 3. f-string 안에서의 따옴표

f-string은 바깥쪽 큰따옴표(`"`)로 감싸입니다. `{...}` 표현식 내부에서 문자열 리터럴이
필요하면 (예: 맵/매치 결과에서 키로 접근할 때) **반드시 홑따옴표(`'...'`)를 사용해야
합니다.** 안쪽에서 큰따옴표를 쓰면 토크나이저가 그 지점에서 f-string이 끝난 것으로
인식합니다. 이것이 `docs/REGEX.md` section 23의 `f"연도: {res['year']}"` 예제가 굳이
홑따옴표를 쓰는 이유입니다 — 이 엔진은 그 관례를 실제로 강제합니다.

`{{`와 `}}`는 각각 리터럴 `{`, `}`로 출력됩니다.

## 4. 선언 시점의 타입 검사

`set <타입> <이름> to <값>`은 **선언되는 순간에만** 값의 런타임 타입이 선언 타입과
일치하는지 검사합니다. `change`는 이후 어떤 타입의 값이든 재대입할 수 있습니다 (타입은
재검사되지 않음) — 이는 언어 전반의 "동적 타입 스타일" 철학과 일치합니다. 유일한
예외는: `empty` 값은 선언된 타입과 상관없이 항상 허용됩니다. 이는 `find`/`match`가
실패했을 때 돌려주는 `empty`를 원하는 타입의 변수에 그대로 담아 두었다가 `or_else`나
`is empty` 검사로 다루기 위함입니다.

## 5. 상수 이름 검사 시점

"전체 대문자가 아닌 상수 이름"과 "상수에 대한 `change`"는 모두 **런타임에 그 문장이
실행되는 시점**에 검사됩니다 (파싱 시점이 아님). 조건부로만 실행되는 코드 안의 상수
선언은 그 분기가 실제로 실행되기 전까지는 검증되지 않습니다.

## 6. 컬렉션 `remove`의 대상 판별

`remove <값> from <컬렉션>`에서 리스트를 대상으로 할 때: 값이 **숫자**면 1-based
인덱스로, 그 외의 값이면 리스트 안에서 **값 자체**를 찾아 제거합니다 (없으면
`ElementNotFoundError`, `or_else`로 잡을 수 있음). 맵을 대상으로 할 때는 문자열 키로
제거하며, 존재하지 않는 키를 지우는 것은 조용히 아무 일도 하지 않습니다 (다른 언어의
관례적인 맵 삭제 동작과 동일).

## 7. `loop match` 제거됨

한때 세 번째 loop 형태로 `loop match 대상 is/IS 상태 do: ... end`가 있었지만,
인터프리터는 이를 `loop while`과 완전히 동일하게 — 매 반복마다 조건을 다시
검사하는 것으로만 — 구현하고 있었습니다 (파서에서 조건식을 파싱하는 코드까지
동일). 실제로 쓰이는 곳이 없어 혼란만 주는 완전한 중복이었으므로 언어에서
제거했습니다. `LoopStmt::LoopKind`에는 이제 `Repeat`/`While` 두 값만 남아
있습니다. 같은 효과가 필요하면 `loop while [조건] do: ... end`를 그대로 쓰면
됩니다.

## 8. 모듈 병합 (`use ... from ...`)

- 모듈 파일은 `실행 중인 스크립트 디렉터리 / <path> / <name>.cuff` 경로로 해석됩니다.
- 모듈의 최상위 함수는 인터프리터 전역 함수 테이블에 등록되어 자동으로 보이게 됩니다.
- 모듈의 최상위 변수는 실행 후 `use`가 호출된 스코프로 복사됩니다.
- 같은 모듈을 두 번 이상 불러오면 (다이아몬드 임포트나 순환 임포트 포함) 두 번째부터는
  조용히 아무 일도 하지 않습니다 — 무한 루프를 방지하면서도 흔한 경우를 안전하게
  처리하기 위함입니다.

## 9. `DLC:*` 내장 라이브러리

명세는 `use DLC:network`라는 예시 하나만 보여줄 뿐 구체적인 라이브러리 목록을 정의하지
않습니다. 이 엔진은 다음을 제공합니다.

| 라이브러리        | 제공 함수                                                                                                                                                                                                         |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DLC:math`        | `math_sqrt`, `math_abs`, `math_pow`, `math_round`, `math_floor`, `math_ceil`, `math_trunc`, `math_sign`, `math_min`, `math_max`, `math_clamp`, `math_mod`, `math_log`, `math_log2`, `math_log10`, `math_exp`, `math_sin`, `math_cos`, `math_tan`, `math_asin`, `math_acos`, `math_atan`, `math_atan2`, `math_pi`, `math_e` |
| `DLC:string`      | `str_upper`, `str_lower`, `str_trim`, `str_trim_start`, `str_trim_end`, `length`, `contains`, `index_of`, `str_starts_with`, `str_ends_with`, `str_repeat`, `str_pad_left`, `str_pad_right`, `str_char_code`, `str_from_char_code` |
| `DLC:time`        | `time_now`, `time_timestamp`                                                                                                                                                                                    |
| `DLC:random`      | `random_float`, `random_int`, `random_seed`, `random_choice`, `random_shuffle`                                                                                                                                  |
| `DLC:list`        | `list_sort`, `list_reverse`, `list_join`, `list_unique`, `list_sum`, `list_average`, `list_flatten`, `list_range`, `length`, `contains`, `index_of` — 전부 원본을 바꾸지 않고 새 값을 반환                    |
| `DLC:map`         | `map_keys`, `map_values`, `map_has_key`, `map_entries`, `map_merge`, `length`, `contains` — 22~24번 항목 참고                                                                                                   |
| `DLC:convert`     | `to_number`, `to_str`, `to_boolean` — 명시적 타입 변환 (핵심 내장 함수이기도 해서 `use` 없이도 항상 씀)                                                                                                        |
| `DLC:json`        | `to_json`, `from_json` — 아래 19번 항목 참고                                                                                                                                                                    |
| `DLC:network`     | `network_get`, `network_post` — 실제 HTTP/1.1 클라이언트. `SECURITY.md`의 "DLC:network" 항목 참고                                                                                                              |
| `DLC:filesystem`  | `file_exist`, `file_size`, `file_read`, `file_readlines`, `file_write`, `file_add`, `file_remove` — 아래 30번 항목과 `SECURITY.md`의 "DLC:filesystem" 항목 참고                                                |

`length`/`contains`/`index_of`만 예외적으로 접두어가 없습니다 — str/list/map
세 자료형에 걸쳐 의도적으로 동일한 다형 구현(`nativeLength`/`nativeContains`/
`nativeIndexOf`)을 공유하도록 설계되어 있어서, `str_length`/`list_length`/
`map_length`로 쪼개면 오히려 "아무 값이나 넣으면 되는" 이 함수들의 장점을
잃게 됩니다. `to_json`/`from_json`/`to_number`/`to_str`/`to_boolean`도 이미
이름 자체에 방향/도메인이 드러나 있어 그대로 두었습니다. 그 외 함수 이름은
전부 `라이브러리_동사` 형태로 통일했습니다 (예: `math_sqrt`, `str_upper`,
`list_sort`) — 이전 버전에서는 `sqrt`, `upper`, `sort`처럼 접두어가 전혀 없어서
어느 라이브러리 소속인지 이름만으로 알 수 없었고, 여러 라이브러리가 같은
이름을 등록하면 (의도치 않은) 충돌 위험도 있었습니다. **이것은 하위 호환을
깨는 변경입니다** — 기존 스크립트는 함수 이름을 전부 새 이름으로 바꿔야
합니다.

`DLC:list`/`DLC:convert`를 추가하며 발견한 것: 라이브러리 이름이 `list`, `count`,
`find`처럼 언어 예약어와 겹치면 `use DLC:list`가 파싱조차 안 되는 버그가 있었습니다
(`ImportParser`가 `IDENTIFIER` 토큰만 이름으로 인정했기 때문). 지금은 IDENTIFIER든
예약어든 "글자로 이루어진 토큰"이면 모두 이름으로 인정하도록 고쳤습니다 — 앞으로
어떤 DLC/모듈 이름을 추가해도 이 문제가 재발하지 않습니다. 이후 같은 문제가
변수/함수/매개변수/루프 변수 이름 선언 전반에도 있었다는 게 드러나서 (`add`,
`count`, `find`, `split`, `replace`, `match`, `in`, `by`, `not`, `global`을 그
자리에 쓸 수 없었음) 28번 항목에서 전체적으로 다룹니다.

## 10. 정규식 매칭의 안전장치

`docs/REGEX.md`가 요구하는 "최대 매칭 스텝 수(Step Limit)와 시간 제한(Timeout)"을
직접 구현했습니다 (`engine/regex/RegexMatcher.h`): 기본값은 스텝 200,000회, 시간
500ms, 재귀 깊이 20,000입니다. 셋 중 하나라도 넘으면 매칭을 멈추고 복구 가능한
`RegexRuntimeError`를 던집니다 (스크립트가 멈추지 않고 `or_else`로 처리할 수 있음).

## 11. 리스트/맵은 참조 타입

리스트와 맵은 `shared_ptr`로 구현되어 있어, 변수 대입이나 함수 인자로 넘길 때 값이
복사되지 않고 같은 저장소를 공유합니다 (Python/JS와 동일). 숫자·문자열·불리언·`empty`는
값으로 복사됩니다.

## 12. 함수는 값이 아님

명세가 명시적으로 클로저/중첩 함수를 지원하지 않는다고 밝히고 있으므로, 함수는
`Value`의 한 종류가 아니라 인터프리터 안의 별도 이름 테이블(`userFunctions_`)로
관리됩니다. 최상위에 있지 않은 함수 선언(다른 함수 안에 중첩된 경우)은
`NestedFunctionNotSupported` 오류를 던집니다.

(v1.7.1 개발 중 한때 이 제약을 풀고 값으로 취급되는 클로저를 도입했었지만,
캡처를 "값으로, 정의 시점에 한 번" 방식으로 구현한 결과 — 스택 기반
`Environment`를 힙 기반으로 바꾸는 훨씬 큰 변경 없이 안전하게 구현할 수 있는
유일한 방식이었음 — 숫자/문자열 같은 스칼라를 캡처한 클로저는 호출 사이에
상태가 누적되지 않는 문제가 있었습니다. 클로저의 "국민 예제"인 카운터 패턴이
동작하지 않는 절반짜리 기능은 이 언어의 방향성(초보자용, 쉽고 가볍게)에
맞지 않는다고 판단해서 제거하고 이 항목으로 되돌렸습니다.)

## 13. `return`/`stop`은 C++ 예외가 아님 (성능 + 정확성)

v0.1.x에서는 `return`/`stop`을 C++ 예외(`ReturnSignal`/`StopSignal`)로 구현해서 함수
호출/루프 경계에서 `catch`로 잡았습니다. 이건 **두 가지 문제**가 있었습니다.

- **성능**: C++ 예외를 던지고 잡는 데는 마이크로초 단위의 비용이 듭니다. 함수가
  `return`할 때마다 매번 이 비용을 냈기 때문에, 재귀가 많은 스크립트에서는 실행 시간
  대부분을 여기서 소모했습니다. 실측: `fib(27)` (호출 약 63만 회) 기준 **2.75초 →
  0.26초로 개선** (약 10.7배).
- **정확성**: `stop`을 잡는 `catch`가 오직 루프 몸통 주위에만 있었기 때문에, 함수 안에서
  (그 함수 자신의 루프 없이) `stop`을 쓰면 함수 호출 경계를 그대로 뚫고 나가서 **그
  함수를 호출한 쪽의 루프까지 끊어버리는** 버그가 있었습니다. 게다가 최상위에서 아무
  루프/함수 없이 `stop`이나 `return`을 쓰면 아무도 못 잡는 예외가 그대로 `main()`
  밖으로 튀어나가 **프로세스가 죽었습니다** (`std::terminate`, SIGABRT).

지금은 `execStatement`/`execBlock`/`execIf`/`execLoop`가 전부 `ExecOutcome`
(`Normal`/`Return`/`Stop` + 값 + 위치)이라는 평범한 값을 리턴해서 위로 전달합니다
(`engine/interpreter/Signals.h`). `catch`는 진짜 `CuffError`(실제 에러)에만 씁니다.
결과적으로:

- `stop`은 `execLoop`가 명시적으로 소비하고, 함수를 호출한 쪽까지는 절대 새어나가지
  않습니다. 함수 안에서 루프 없이 `stop`을 쓰면 이제 `StopOutsideLoop`라는 깔끔한 런타임
  오류가 됩니다 (크래시 아님).
- 최상위에서 `return`/`stop`을 쓰면 `ReturnOutsideFunction`/`StopOutsideLoop` 오류로
  깔끔하게 처리됩니다 (역시 크래시 아님).

새 제어 흐름을 추가할 때(예: `continue`, `break with label` 등)는 `ExecResult`에 새
케이스를 추가하고, 그 신호를 "소비"해야 하는 지점(루프? 함수?)에서 명시적으로 처리하면
됩니다 — 예외를 다시 꺼내 쓰지 마세요, 이번에 없앤 성능 문제가 그대로 돌아옵니다.

## 14. 문자열 인덱싱/슬라이싱은 UTF-8 코드포인트 기준

`str[i]`, `str[i~j]`, `length()`(DLC:string)는 바이트가 아니라 **UTF-8 코드포인트** 단위로
동작합니다 (`engine/interpreter/Utf8.h`). 한글은 완성형 한 글자가 코드포인트 하나이므로
`"안녕하세요"[1]`은 "안"을 정확히 반환합니다 — 이전에는 바이트 단위였어서 한글 문자열을
인덱싱/슬라이싱하면 멀티바이트 시퀀스 중간이 잘려 깨진 바이트가 나왔습니다.

그래핌 클러스터(자모 결합, 이모지 ZWJ 시퀀스 등)까지는 다루지 않습니다 — 코드포인트
단위로 충분한 이유는 한글이 조합형이 아니라 완성형(NFC)으로 입력되는 한 각 글자가 이미
코드포인트 하나이기 때문입니다. `contains`/`starts_with`/`ends_with`/`+`(연결)은 원래도
바이트 단위 그대로 두었습니다 — UTF-8은 자기동기화 인코딩이라 바이트 단위 검색/결합이
코드포인트 경계를 침범하지 않아 이미 안전하게 동작합니다.

정규식 엔진도 코드포인트 단위로 동작합니다 — 아래 17번 항목 참고.

## 15. 타입 변환 (`to_number`/`to_str`/`to_boolean`)은 기본 내장

`use DLC:convert` 없이 바로 쓸 수 있습니다 (`registerBuiltins`가 내부적으로
`registerConvertDLC`를 호출). `use DLC:convert`를 써도 여전히 동작합니다 — 같은 함수를
한 번 더 등록할 뿐이라 무해합니다. 다른 DLC 함수들(`math`/`string`/`list`/`random`/`time`)은
여전히 `use`가 필요합니다 — `input()`으로 받은 문자열을 숫자로 바꾸는 건 매우 흔한 패턴이라
기본 제공으로 옮겼지만, 나머지까지 전부 기본 내장으로 만들면 `use` 자체의 의미가 없어지므로
그렇게 하지 않았습니다.

## 16. 회귀 테스트 (`tests/`)

`bash tests/run.sh` — `tests/cases/`(성공 케이스, 출력 diff), `tests/errors/`(실패 케이스,
에러 코드 확인), `examples/`, `examples/error_cases/`를 전부 돌립니다. 새 기능/버그 수정
시 관련 케이스를 `tests/`에 같이 추가하세요 (`tests/README.md` 참고). `.github/workflows/
build-and-test.yaml`에서 push/PR마다 자동 실행됩니다.

이 테스트를 만드는 과정에서 실제 버그 2개를 발견/수정했습니다:

**정규식 재귀 깊이 제한이 너무 높아서 진짜 스택 오버플로우(SIGSEGV)가 났음.** `[any]+`처럼
선형으로 깊게 재귀하는 패턴을 25,000자 문자열에 매칭하면 프로세스가 죽었습니다 —
`depthLimit`을 20,000으로 잡아뒀는데, 실측해보니 8MB 스택 기준 실제 크래시는 ~19,500
근처에서 이미 발생해서 제가 만든 안전장치(예외 던지기)가 발동하기도 전에 진짜 스택이
터진 것입니다. `depthLimit`을 3000으로 낮췄고(관측된 크래시 지점 대비 6배 이상 여유),
100자부터 100만자까지 전 구간에서 크래시 없이 정상 종료 또는 `RegexRecursionLimitExceeded`
(E3-103)를 깔끔하게 던지는 것을 확인했습니다. 교훈: "충분히 보수적"이라고 생각한 숫자도
실측 없이는 믿으면 안 됩니다.

**식별자로 예약어를 쓸 수 없는 범위가 생각보다 넓음 (미해결, 기록만 해둠).**
`set number add to 5`, `set returnable func add(...) do:` 둘 다 파싱 에러가 납니다 —
`add`/`count`/`find`/`split`/`replace`/`match`/`in`/`by`/`not`/`global` 등, 문법 키워드로
쓰이는 흔한 단어들을 변수명/함수명으로 전혀 쓸 수 없습니다. `ImportParser`에서 DLC 이름이
같은 문제였던 것과 동일한 원인(`TokenType::IDENTIFIER`만 엄격히 검사)인데, 이번엔
`DeclarationParser`/`FunctionParser` 등 이름을 선언하는 모든 지점에 퍼져있어서 범위가 더
큽니다. 아직 고치지 않았습니다 — `tests/errors/argument_count_mismatch.cuff`가 원래
`add`라는 함수명을 쓰려다 이 문제에 걸려서 `combine`으로 우회했습니다.

## 17. Regex matching is codepoint-based (not byte-based)

_(Notes from here on are written in English.)_

The matcher walks the subject string one **UTF-8 codepoint** at a time, not one byte.
Positions are still stored as byte offsets internally — that keeps `substr()` on captures
free and avoids building an index table per match — but every place that _advances_ a
position now consumes a whole codepoint (`engine/common/Utf8.h`).

What this changes, concretely:

- `[any]` matches one codepoint. `"안녕하세요" is "[any]5"` is now true (it was false
  before, because the string is 15 bytes).
- A literal non-ASCII character written directly in a pattern (`"안녕"`) is compiled into a
  single multi-byte literal node and compared as one unit, instead of byte-by-byte.
- Negated sets (`[!num]`, `[!a-z]`) match a non-ASCII codepoint. This is the one case where
  a named class _can_ apply to multi-byte text, and it's handled by testing the whole
  codepoint rather than each byte.
- `search()` only ever starts an attempt at a codepoint boundary, and `[one:...]`
  alternatives must also _end_ on one — otherwise a match could span a partial character
  and `substr()` would produce mojibake.
- `[edge]` treats any non-ASCII codepoint as a word character, so `[edge]안녕[edge]` works.
  This follows the spec's own definition (REGEX.md section 17: the boundary between a word
  and whitespace/punctuation/string-start/end).

What deliberately stays ASCII-only, per the spec's explicit wording:

- `[let]` (영문 알파벳 / English alphabet), `[low]`, `[up]`, `[str]` (영문자 + 숫자),
  `[word]` (영문자 + 숫자 + 언더바 — an _identifier_ class), `[num]`, `[hex]`, and the
  `[int]`/`[float]`/`[email]`/`[phone]`/`[url]` presets. `"안녕" is "[let]+"` is false.
- `[any]` is the token for "any character in any language" — REGEX.md describes it as
  "줄바꿈을 제외한 세상의 모든 글자 및 기호".

So `[word]` excluding Korean while `[edge]` includes it is not an inconsistency: `[word]` is
documented as an identifier class, `[edge]` as text segmentation. They serve different jobs.

Case-insensitive matching (`IS`, flag `i`) remains ASCII-only folding — correct Unicode case
folding needs a real Unicode table, and it is a no-op for scripts without case (Hangul, CJK,
Thai, ...), which is the common case here. Grapheme clusters (combining jamo, emoji ZWJ
sequences) are likewise out of scope, same reasoning as section 14.

## 18. Indices and range bounds must be whole numbers

`list[i]`, `str[i]`, `x[i~j]`, and `loop repeat i to A ~ B` all reject a fractional value
with `FractionalIndex` (E4-024) instead of silently rounding it, which is what the engine
used to do. A computed index that lands on `2.5` almost always means the _calculation_ is
wrong; rounding it hides the bug and produces a plausible-looking wrong answer.

Whole-valued doubles still work, since CuffScript has a single `number` type and `6 / 2`
legitimately produces `3.0` — only genuinely fractional values are rejected. Round
explicitly (`round`/`floor`/`ceil` from `DLC:math`) when that's what you mean.

## 19. `DLC:json`

JSON maps onto the value model almost exactly, so the mapping is the obvious one:
object ↔ `map`, array ↔ `list`, string ↔ `str`, number ↔ `number`, `true`/`false` ↔
`boolean`, `null` ↔ `empty`. `to_json(value)` produces compact output;
`to_json(value, indent)` pretty-prints with `indent` spaces (0-10).

Parsing is deliberately **strict** (RFC 8259): trailing commas, single-quoted strings,
unquoted keys, leading zeros, `NaN`/`Infinity`, and trailing content after the value are all
rejected. Being lenient here is how malformed data gets into a system unnoticed — a parse
error at the boundary is much cheaper than a wrong value deep inside a program. `\uXXXX`
escapes are decoded, including surrogate pairs, so `"\ud83d\ude00"` round-trips to a real
emoji rather than two broken halves. Output leaves UTF-8 bytes unescaped, keeping Korean and
emoji readable instead of `\uXXXX` soup.

Serializing a `match` result, `NaN`, or `Infinity` fails rather than inventing a
representation JSON doesn't have. Nesting is capped at 200 levels so a hostile input can't
overflow the stack during parsing.

## 20. Error-code hygiene

Two conventions, both checked during a full audit of every throw site:

- **Message capitalization is uniform**: every error message starts lowercase, because it's
  always rendered after a `...at line N, column M: ` prefix. (Parser messages used to start
  uppercase while runtime messages started lowercase.)
- **`ArgumentError` means the wrong _number_ of arguments; `ValueError`
  (`InvalidArgumentValue`, E4-025) means the right number but a value the function can't
  use** — `sqrt(-1)`, `to_number("abc")`, malformed `from_json` input, `random_int(5, 1)`.
  These previously all reported as E4-010 `ArgumentCountMismatch`, which was simply the wrong
  code for them.

When adding a builtin, pick the code by what actually went wrong, and keep the message
lowercase. Add a hint only when there's a concrete next action for the reader — a hint that
just restates the message is noise.

## 21. Interpreter performance work

Two structural changes, both driven by `gprof` profiles of a recursion-heavy benchmark
rather than guesswork. The lesson from both: the bottleneck was not where it seemed.

**Operators are enums, not strings.** `BinaryOp`/`UnaryOp` used to store the operator as a
`std::string` (`"+"`, `"is"`, ...) and `evalBinaryOp` dispatched with an
`if (op == "is") ... else if (op == "+")` chain. The profile showed **16.2 million string
comparisons** for a 635k-call benchmark — roughly 25 per call, by far the single largest
cost in the engine. The chain is now a `switch` over `BinOp`/`UnOp` enums assigned at parse
time. Measured on `fib(27)`: 0.241s → 0.149s (38% faster).

**Variable and function names are interned to integer IDs** (`engine/common/NameInterner.h`).
The parser assigns each identifier a dense `uint32_t`; `Environment` stores and compares
those instead of strings, so a scope lookup is an int compare over a contiguous vector. A
benchmark with 400 globals went from 0.059s to 0.027s. Names are still kept alongside the
IDs for error messages, and a string-taking `declare` overload remains for the cold module-
merge path.

Where it stands now, versus before any of this session's work:

| benchmark                       | before |  after |
| :------------------------------ | -----: | -----: |
| `fib(27)` (635k calls)          | 0.241s | 0.140s |
| 2M-iteration loop with `change` | 0.287s | 0.173s |
| 400 globals, 200k lookups       | 0.059s | 0.027s |
| 3-argument call overhead        |  252ns | ~145ns |

**What would come next, and why it hasn't been done.** The profile is now dominated by
`evalExpr` itself — AST node dispatch and `Value` copies — which is the intrinsic cost of a
tree-walking interpreter. Getting substantially past this means compiling to bytecode and
running a stack VM, which is a rewrite of the execution core rather than a refactor of it.
That is a reasonable next step if a real workload demands it; it is not worth the risk on
speculation, and the current engine is in the same performance range as CPython on
call-heavy code.

## 22. Recursion safety and resource limits

Every native-stack recursion in the engine is now bounded, so no script can crash the
process; hostile or accidental input ends in an ordinary error code instead. All numeric
limits live in one file, `engine/common/Limits.h`.

**Parser.** `ParseDepthScope` (in `ParserCore.h`) counts native recursion across all parsers,
including the nested parser used for f-string expressions, and fails with `E2-008` beyond
256 levels of nested parentheses, lists, maps, unary chains or blocks. Chains that the parser
builds iteratively (`1+1+1+...`, `a[1][1]...`) are bounded by `Expr::height`, computed when
each node is constructed (`E2-008` beyond 10,000). Source larger than 16 MiB is rejected
(`E2-009`).

**Evaluator.** A depth counter alone cannot bound stack use: a function body with many
nested blocks uses far more stack per call than a flat one. So `evalExpr` and `execStatement`
compare the real stack pointer with a budget derived from the actual stack size (on Linux,
`pthread_getattr_np`; elsewhere a fixed default, `Interpreter::Config::stackBudgetBytes` to
override). Exceeding it raises the catchable `E4-017`, the same code as the call-depth limit
(1,000 calls, unchanged). Regex matching checks a hard floor below that budget, so it fails
with `E3-103` rather than overflowing when the interpreter is already deep.

**Values.** Nested lists/maps are destroyed iteratively (`dismantleValues`), and `is` compares
iteratively, so depth never matters; circular structures of the same shape compare equal.
Printing marks cycles (`[...]`, `{...}`) and stops at depth 1,000. `to_json`/`from_json` stop at
depth 200.

**Sizes.** Strings are capped at 128 MiB and lists/maps at 32M items (`E4-026`), enforced where
they can grow: `+`, f-strings, `join`, `repeat_str`, padding, `range`, `flatten`, `merge`,
regex `replace`/`find`/`split` results and the async task queue. Running out of memory anyway
is reported as `E6-003` instead of terminating the process.

**Execution budget (opt-in).** `--max-steps N` and `--timeout MS` (or `CuffEngine::Options`)
bound loop iterations plus function calls, and wall-clock time. Both are off by default. They
raise `E6-001`/`E6-002`, which live in the 6000 range and are deliberately **not** catchable by
`or_else`, so a script cannot swallow its own kill switch.

**Regex.** Group nesting is capped at 64 and quantifier counts at 100,000; a pattern over
64 KiB is rejected. Each find/replace/split/count also has an overall time allowance
(5 s plus 2 s per MiB of input) on top of the existing per-attempt limits, and the compile
cache is bounded (512 entries).

Tuning: the numbers were chosen so that ordinary programs never notice them (10M-element
lists, 1,500-term expressions, 2M regex matches and 990-deep recursion with rich bodies all
still work). Raise them in `Limits.h` if a workload needs more.

## 23. Module sandbox

`use name from path` may only load files inside a root directory: the running script's
directory by default, or `--root <dir>` / `CuffEngine::Options::rootDir`. Absolute paths and
paths that resolve outside the root (`..`, or a symlink pointing out, checked on the
canonicalized path) fail with `E5-006`. A module must be a regular file no larger than the source
limit, and imports may nest 64 levels (`E5-007`). Errors show the path as written in the script
rather than the host's absolute path. A failed import no longer marks the module as loaded,
and its AST is kept alive even if its body fails, so functions it registered can never dangle.

## 24. Built-in functions and performance

**Added.** `type_of` (always available); `DLC:math` `mod` (floored), `clamp`, `sign`, `trunc`,
`log`, `log2`, `log10`, `exp`, trig functions, `pi`, `e`, `round(x, digits)`; `DLC:string`
`trim_start`, `trim_end`, `index_of`, `repeat_str`, `pad_left`, `pad_right`, `char_code`,
`from_char_code`; `DLC:list` `sum`, `average`, `flatten`, `range`; `DLC:random` `random_seed`,
`choice`, `shuffle`; and a new `DLC:map` with `keys`, `values`, `has_key`, `entries`, `merge`.
`length`, `contains` and `index_of` work on strings, lists and maps and are available from
whichever of `DLC:string`/`DLC:list`/`DLC:map` is imported. `min`/`max` also accept a list.

**Hardened.** `sort`, `min`, `max` and `clamp` reject NaN instead of misordering (sorting NaN
was undefined behavior). `pow`, `log`, `asin`, `acos`, `mod` reject domain errors and results that
are not finite. `to_number` accepts only plain decimal text (no `nan`, `inf`, hex floats or
trailing garbage). Whole-number arguments are range-checked to +/-2^53 (`random_int` used a
32-bit `long` under WebAssembly). `unique` is O(n) for numbers and strings. `upper`/`lower`
map Latin, Greek and Cyrillic letters, not just ASCII. Number formatting no longer builds an
`ostringstream` per value.

**Performance.** Strings are immutable and shared (`StrData`): reading a variable or passing a
string never copies its text, literals are built once at parse time, and ASCII-ness, codepoint
count and a codepoint cursor are cached, so indexing and `length` are O(1) for ASCII text and
amortized O(1) for sequential access on other text (a 1 MB string read 20,000 times went from
19.7 s to 6 ms; a 20,000-character index loop from 0.55 s to 12 ms). Environments keep their
name index up to date incrementally (20,000 globals: 3.8 s to 0.05 s). Native and user
functions are looked up by interned id; cold error paths are out of line, which shrinks the
hot recursion frames and the stack used per call by about a quarter. Tokens are moved rather
than copied between pipeline stages.

## 25. v2.0.0: nesting ceiling, tuples, `pure`, `func`, and DLC:network

**Nesting depth raised to 512.** `limits::kMaxParseDepth` (256 -> 512) governs source-level
nesting: parentheses, list/map literals, and `if`/`loop`/`or_else` blocks. Raising the counter
alone would have been unsafe on a small native stack (the parser recurses on the real C++
stack with no bytecode to unwind into) — measured empirically, 512 levels of nested
parentheses can need over 1 MiB of native stack, more than Windows' 1 MiB default thread
stack. The parser now also primes a stack-pointer floor at the start of every `Parser::parse()`
call (`ParseStackFloorScope`/`parseStackFloor()` in `ParserCore.h`), sized from the real
available stack (`availableStackBytes()`, extended below) and checked by every
`ParseDepthScope`, mirroring the interpreter's own stack guard. Other depth-like limits
(`kMaxRegexNesting`, `kMaxImportDepth`, `kMaxValueDepth`) were left as they were — this change
is scoped to source nesting only, not the size/collection/regex limits from v1.6.0.

**`availableStackBytes()` is now cross-platform** (`engine/common/Attributes.h`): Linux via
`pthread_getattr_np`, macOS via `pthread_get_stackaddr_np`/`pthread_get_stacksize_np`, Windows
via `GetCurrentThreadStackLimits`, and Emscripten via `emscripten_stack_get_free()`. Previously
Linux-only; both the parser's and the interpreter's stack guards now adapt to the real stack
everywhere instead of guessing on three of the four shipped targets.

**`constant list` (tuple).** `constant` now works on `list` as well as scalars, giving a
Python-tuple-style read-only list — no new value type: `ValueList` gained one field,
`isConstant`, checked at every mutation site (`add`/`remove`, and the final container in
`resolveContainerSlot`, which is also what `change x[i] to v` and CollectionOp's `Replace` go
through). Declaring one (`execDeclaration`'s `freezeList`) always copies the initializer into a
fresh `ValueList` before freezing it, specifically so `set constant list T to existing_var`
can't reach back and silently freeze `existing_var`'s own list — `List` is a reference type
(`engine/interpreter/Value.h`), so without a defensive copy the two names would share one
backing object. The immutability travels with that object, not with any one variable name: an
alias or a function argument bound to the same frozen list is blocked too (`execChange`'s
existing name-based constant check was narrowed to whole-variable reassignment only; an
indexed write now falls through to the value-level check, which is what makes both the alias
case and shallow nesting — mutating a plain list found _inside_ a frozen tuple is allowed, only
the tuple's own slots are frozen — work correctly). `constant map` was not added; the ALL-CAPS
naming rule is unchanged and applies to constants of every type as before.

**`function` renamed to `func`.** A straight keyword replacement (`lexer/KeywordClassifier.h`),
not an alias — existing scripts using `function` need a one-word find-and-replace. Every
example, test fixture, and doc code sample was updated.

**`pure` functions.** A new independent modifier (`async`/`returnable`/`pure` combine in any
order): `set pure func f() do: ... end`. Enforcement lives entirely in `Interpreter`, not
`Environment` — `FrameGuard` now also carries `isPure`, and the three places that resolve a
name against an `Environment` (`evalExpr`'s identifier case, `execChange`, `execCollectionOp`)
each check `currentFunctionPure_ && look.owner == &globalEnv_` and raise the new
`PureFunctionGlobalAccess` (E4-027) before the read/write happens; `change x to global` is
rejected at the bridge itself, before it can even register. The restriction is on that
function's own body, not the whole call graph: a pure function calling a non-pure one is fine,
and the callee's own purity (or lack of it) governs its own body, exactly as `returnable` and
`async` already worked per-call via the same `FrameGuard`. Removing `pure` is a complete,
literal escape hatch, by construction — there's no separate override flag to keep in sync.

**`DLC:network`.** A dependency-free HTTP/1.1 client, `engine/net/HttpClient.h`: POSIX sockets
on Linux/macOS, Winsock2 on Windows (the one file in the engine with `#ifdef _WIN32` socket
code). Plain HTTP only — no TLS, so `https://` fails with a clear error rather than silently
talking plaintext to an HTTPS port. `get(url)` and `post(url, body[, content_type])` return
`{"status", "ok", "body"}`; connection/DNS/timeout failures raise `NetworkRequestFailed` (E4-028),
catchable via `or_else` like any other runtime error. Handles chunked transfer-encoding,
`Content-Length` framing, and 301/302/303/307/308 redirects (303, and 301/302 for an original
POST, downgrade to GET per common client behavior; 307/308 preserve method and body), each
capped (`limits::kHttpMaxRedirects`). Every resolved address is checked against loopback/
private/link-local ranges (`net::isPrivateOrLoopback`, IPv4 and IPv6, checked on the concrete
`sockaddr` right before connecting, not the hostname text) and rejected unless
`CuffEngine::Options::allowPrivateNetworkTargets` opts in — the same "safe by default, widen
explicitly" shape as the module sandbox's `rootDir`. `networkEnabled` (default true) lets an
embedder turn `DLC:network` off entirely for untrusted scripts. Both are also CLI flags
(`--allow-private-network`, `--no-network`). See `SECURITY.md` for the reasoning.

**Windows build fix.** Cross-compiling with `x86_64-w64-mingw32-g++` surfaced a real bug:
`<windows.h>` (pulled in by `Attributes.h` for `GetCurrentThreadStackLimits`) `#define`s `TRUE`,
`FALSE` and `IN` as bare macros, silently mangling `TokenType::TRUE`/`FALSE`/`IN` wherever
`Attributes.h` was included first (`TokenType::TRUE` would macro-expand to `TokenType::1`,
failing to compile). Fixed with `WIN32_LEAN_AND_MEAN`/`NOMINMAX` plus targeted `#undef`s right
after the `windows.h` include. The Windows build was not previously exercised with a real
toolchain; it now compiles cleanly under mingw-w64 (`x86_64-w64-mingw32-g++`, `-lws2_32`).
Actually running the cross-compiled binary could not be verified in this environment (no
working Windows/Wine runtime available) — only compilation was confirmed.

**Performance.** Two changes, both measured, both kept because neither regresses anything:
`std::get<T>` calls in the `evalExpr`/`execStatement` dispatch were replaced with
`std::get_if<T>` (avoids the exception-handling path `std::get` carries even though the kind
check already guarantees the right alternative), and `evalCall`/`invokeAwaited`/
`callUserFunction` now recycle their argument vector and the callee `Environment`'s variable
storage across calls (`Interpreter::argsPool_`/`envVarsPool_`, capped, RAII-returned on every
exit path including exceptions) instead of allocating fresh each call. Two other ideas were
tried and measured to have no real benefit on this toolchain (glibc 2.39), so were **not**
adopted as load-bearing optimizations: replacing `shared_ptr`'s atomic refcounting with a
non-atomic one (glibc's `__libc_single_threaded` fast path already makes single-threaded
`shared_ptr` copies non-atomic in practice) and a broader allocation-pooling scheme beyond the
call path above (glibc's per-thread tcache already caches same-size small allocations about as
well as a hand-rolled pool would). Net effect: recursive/call-heavy code (`fib(30)`) is
modestly faster (~7%); pure arithmetic loops, which make no allocations to begin with, are
unchanged — a tree-walking evaluator's per-node cost was already close to its practical floor
after v1.6.0's work, and closing that gap further would mean the bytecode VM this project is
deliberately deferring, not a tuning pass.

---

## 26. Pure-function hardening: call-graph propagation

`pure` originally only checked a function's own body (see 25번). That left a trivial
escape hatch: `set pure func f() do: g() end` where `g` touches globals — `f` never
touches a global *directly*, so the old check passed, but calling `f` still reached a
global through `g`. `checkPureCallAllowed()` (`Interpreter.h`) now runs at every call
site (`evalCall`, `invokeAwaited`) and rejects a pure caller invoking a non-pure
*user-defined* function, with a new error, `PureFunctionImpureCall` (E4-029).
Native/DLC functions are still exempt — they never touch a CuffScript `Environment` at
all, so they can't reach a global through this route regardless. Recursion and
pure-calling-pure remain unrestricted, since `decl.isPure` is checked per callee, not
per call depth. This is a **behavior change**: `tests/cases/pure_functions.cuff`'s old
"pure calling non-pure is fine" example no longer holds and was rewritten; the same
scenario now lives in `tests/errors/pure_func_call_impure.cuff` as an E4-029 case.


## 27. Global-bridge bug: `Environment::resolve()` checked the bridge before the local

`change x to global` (25번, and the earlier feature it hardens) had a real, reproducible
bug: `resolve()` checked whether a name was in the current function scope's
`globalDeclared_` list *before* checking for an actual local (`findLocal`) at each level.
Once a name was bridged, any later *local* redeclaration of the same name in that same
call — most visibly a `loop repeat x to 1 ~ 3` reusing a bridged `x` as its loop
variable — became permanently invisible to reads for the rest of that call: every read
saw the stale global instead of the loop's own counter, silently, with no error. The fix
is a one-line reordering: check `findLocal` first at every level (including the function
scope itself), and only fall back to `globalDeclared_`/true-global when no local exists
— restoring the same "innermost declaration always wins" rule the rest of the language
already follows. This also incidentally fixes the same class of misattribution in the
`pure` global-access check (`currentFunctionPure_ && look.owner == &globalEnv_`), which
could previously misreport an ordinary shadowed local as forbidden global access.
Regression test: `tests/cases/global_bridge_shadowing.cuff` (loop-variable shadowing and
the `or_else` declaration-recovery placeholder, which hits the identical bug from a
different angle).

## 28. Reserved words as identifiers

`add`, `count`, `find`, `split`, `replace`, `match`, `in`, `by`, `not`, and `global`
could not be used as a variable, function, parameter, or `loop repeat` variable name —
every name-declaring/-referencing spot checked `TokenType::IDENTIFIER` specifically,
which a reserved word never is, even though the exact same problem for DLC/module names
was already fixed by relaxing that check to `isWordLikeToken()` (9번). The fix does the
same thing everywhere else names are declared: `DeclarationParser` (`set` variable name,
`change` target), `FunctionParser` (function name, parameter names), `CollectionOpParser`
(the collection name in `add`/`replace`/`remove`), `LoopParser` (the `repeat` loop
variable), and `RegexExprParser::looksLikeCollectionReplace` (the 2-token lookahead that
tells the statement and expression forms of `replace` apart) all now accept any
word-shaped token via the shared `isWordLikeToken()` (moved from `ImportParser.h` to the
more central `ParserCore.h`).

Reading one of these names back in an *expression* needed separate handling in
`LiteralParser::parsePrimary` (`ExpressionParser.h`), since that's the one place a
keyword's token type is dispatched on to decide what to parse:
- `add`, `to`, `in`, `by`, `global`, `not`, `from` have no meaning anywhere in expression
  grammar — every existing use of each is consumed via an explicit `p.consume(...)` at a
  fixed grammar point, never checked as "does an expression start here". They're always
  safe as a bare identifier reference (`isBareIdentifierKeyword()`).
- `match`/`find`/`replace`/`split`/`count` are different: each already has its own
  primary-position construct (`match X from Y`, `count "p" in y`, ...). Disambiguating
  "the construct" from "a bare name of the same word" uses one token of lookahead
  (`canStartExpressionToken()`/`looksLikeConstructContinuation()`): if what follows can't
  plausibly start that construct's required sub-expression, it's a name instead. `(` and
  `[` are deliberately excluded from that lookahead even though they can start an
  expression, because `split(...)`/`count[0]` need to mean "call/index the
  variable/function named split/count" (handled by ordinary postfix parsing after
  `parsePrimary` returns a plain identifier) — including them would make it impossible to
  ever call or index something with one of these five names again.

This is necessarily a judgment call about which collisions are safe to resolve
mechanically, not a complete lifting of every keyword restriction — see the note on
`SPEC.md` §1 for exactly which words are covered. `not`'s only other grammar role is the
`is not` comparison (`parseComparison`'s own `p.check(TokenType::NOT)`, unrelated to
`parsePrimary`), so there's no interaction between using `not` as a variable and using it
in an `is not` comparison. Regression test: `tests/cases/reserved_words_as_identifiers.cuff`.

## 29. Caret (`^`) marker in error messages

Errors now show the offending source line with a `^` under the exact column, e.g.:

```
[E2-001] Syntax Error at line 4, column 10: unexpected token ')' in expression
    print(x +)
             ^
```

`CuffError`/`SourceLocation` themselves are unchanged — a raw `CuffError::what()` still
renders message+hint only, with no source access. The snippet is built one layer up, in
`CuffEngine::execute()`/`run()`, exactly where the original source text is still in scope
when a `CuffError` is caught: `buildCaretSnippet()` finds the line via `loc.offset` (a
byte offset already kept in sync with the tokenizer's `line`/`column`, see
`ScanState.h`), and `renderErrorWithSnippet()` reconstructs the full message from the
error's own public fields with the snippet spliced in between message and hint, rather
than parsing `what()`'s already-flattened string. The caret's horizontal offset is
counted in **codepoints**, not bytes — `column`/`offset` are byte-based (one UTF-8
continuation byte = one column), so a Korean/emoji/etc. string literal earlier on the
same line would otherwise push the caret roughly 3x too far right; the byte-prefix up to
the error's offset is measured with `utf8::length()` instead. Regression test:
`tests/unit/error_snippet_test.cpp` (ASCII, UTF-8-alignment, and a runtime-error case).

## 30. `DLC:filesystem`

Real local file access: `file_exist`, `file_size`, `file_read`, `file_readlines`,
`file_write`, `file_add`, `file_remove` (`registerFilesystemDLC`, `engine/dlc/FilesystemDLC.h`).
Every path is resolved relative to, and confined inside, the exact same sandbox root
`use ... from` module imports already use (`Interpreter::moduleRoot_`, i.e.
`CuffEngine::Options::rootDir` or the script's own directory) — there's no separate,
filesystem-specific root to configure. `isInsideRoot()` (the containment check module
loading already had) moved from a private `Interpreter` method to a shared
`engine/common/PathSandbox.h` so both features use the identical check rather than two
copies that could drift. An absolute path, or a relative path that escapes the root
(`../../etc/passwd`), is rejected with a new error, `FilesystemAccessDenied` (E5-008),
*before* touching the filesystem — a script trying to reach outside its sandbox is worth
surfacing loudly. An ordinary OS-level failure once a path clears that check (file
doesn't exist, permission denied) is reported the quiet way each function's own contract
promises (`empty`/`false`), not by throwing — `file_exist()` is how a script is expected
to check first. `file_read`/`file_readlines` check the file's stat'd size against the
engine's normal string-size ceiling *before* allocating or reading anything (rather than
slurping the whole file first and rejecting after), so an oversized file on the sandboxed
filesystem can't force an oversized allocation just to get rejected. New host controls,
mirroring `DLC:network`'s existing shape exactly: `CuffEngine::Options::filesystemEnabled`
(default true) / `--no-filesystem`. See `SECURITY.md`'s "DLC:filesystem" section for the
full trust-boundary discussion. Regression tests: `tests/cases/dlc_filesystem.cuff`
(success paths, self-cleaning), `tests/errors/dlc_filesystem_disabled.cuff` (E5-005),
`tests/errors/filesystem_sandbox_escape.cuff` (E5-008).

## 31. DLC function naming convention (`library_verb`)

Every DLC function name (except the ones noted below) now carries its library as a
prefix — `sqrt` → `math_sqrt`, `upper` → `str_upper`, `sort` → `list_sort`, `now` →
`time_now`, `random` → `random_float`, `choice` → `random_choice`, `keys` → `map_keys`,
`get`/`post` → `network_get`/`network_post`, and so on (see the table in 9번 for the
complete mapping) — matching the `file_*` shape `DLC:filesystem` (30번) introduced.
Reasoning: before this, a script reading `sort(x)` or `get(url)` had no way to tell which
`use`d library it came from without cross-referencing every `use` line, and two libraries
registering the same bare name (all of them share one flat `natives_` map) could silently
shadow each other with no warning. Three exceptions, deliberately not renamed:
`length`/`contains`/`index_of`, which are intentionally polymorphic across str/list/map
(registered identically under all three — see 11번) and would lose that "works on
anything" property if split into `str_length`/`list_length`/`map_length`; and
`to_json`/`from_json`/`to_number`/`to_str`/`to_boolean`, whose names already encode
their domain and direction without a prefix. **This is a breaking rename** — every
example, test, and doc snippet using the old bare names was updated in this repository,
but any script outside it needs the same mechanical find-and-replace per function.

## 32. `loop match` removed

`LoopStmt::LoopKind` had three values — `Repeat`, `While`, `Match` — but `execLoop`
(`Interpreter.h`) implemented `Match` with the exact same code path as `While` (re-check
a boolean condition every iteration), and `LoopParser` parsed `loop match target is/IS
state do: ... end`'s condition the same way `while`'s condition is parsed. It was a
complete, confusing duplicate with no functional difference and no use in this
repository's examples or tests, so it was removed outright rather than kept as sugar:
`LoopKind` now has just `Repeat`/`While`, `loop match ... do:` is a plain syntax error
("expected 'repeat' or 'while' after 'loop'"), and `ASTPrinter`'s debug dump for loops
was simplified to match. `loop while [condition] do: ... end` covers the exact same case.

## 33. Async: reasoned decision not to add OS threads, plus a light pass

The brief for this release asked for "real" async concurrency and left the choice between
non-blocking I/O and multithreading — and the resulting thread-safety work — to be
decided here. The two honestly available options, given this is a recursive tree-walking
evaluator with no coroutine/continuation support (no way to suspend a call mid-statement
and resume it later on the same native call stack):

1. **OS threads**, with a GIL-style lock serializing all interpreter execution and
   released only around a queued task's raw blocking socket I/O — the only way to get
   genuine wall-clock overlap between multiple queued tasks' network calls without a
   deeper rewrite. This was fully designed (thread-local per-call-stack frame state to
   stop concurrently-running tasks from corrupting each other's `pure`/`returnable`/
   call-depth bookkeeping; a FIFO-preserving spawn order so scripts with no I/O keep
   today's exact sequential output; exception propagation across worker threads).
2. **Leave the current cooperative model as-is** and be transparent about its actual
   shape rather than overselling it as concurrency it doesn't have.

**Decision: option 2.** Real, safe concurrent execution fundamentally needs either OS
threads or turning the evaluator into a resumable state machine; the latter is a rewrite
far beyond this change's scope, and the former is a permanent complexity and correctness
tax on every future change to `Interpreter.h` — every field on that class becomes a
question of "is this per-call-stack state that now needs to be thread-local", for a
feature (overlapping *network* I/O specifically, since that's the only thing in this
engine slow enough for overlap to matter) that most scripts written in this language will
never exercise. That tradeoff cuts directly against this language's own stated design
goals — simple, lightweight, easy to reason about — more than any other single change in
this release; see the more general version of this argument given directly to the user
alongside it. **What "async" means here remains unchanged and is now documented as such
rather than implied otherwise**: `f()` on an `async` function queues it; the queue drains,
strictly in FIFO order, once the top-level script's synchronous code finishes (self-queued
tasks included, see `examples/10_async_ordering.cuff`); `await f()` runs it immediately,
synchronously, and returns its value. This is cooperative scheduling, not concurrency —
useful for controlling *when* code runs relative to the rest of the script, not for
making two things run at the same wall-clock time.

A later, smaller pass over this same area (after the architectural question above was
already settled) found two real, if minor, bugs and locked in two behaviors that were
already correct but untested:
- Two error hints quoted the wrong keyword — `'set async function NAME(...) do:'` and
  `'set returnable function'` — when the actual declaration keyword is `func`, not
  `function` (`throwAwaitOnNonAsync`, `throwReturnInVoidFunction`). Fixed; these are the
  only two spots that quoted the syntax literally rather than using "function" as the
  English word for it.
- An error thrown by a queued task now has a regression test confirming it stops the
  drain the same way an error mid-script stops synchronous execution — tasks queued after
  the failing one don't run (`tests/errors/async_error_stops_queue.cuff`) — this was
  already the behavior (an uncaught exception simply unwinds out of `drainTaskQueue`), just
  previously unverified by a test.
- Self-queuing chains (a queued task queuing another queued task queuing another, `A` →
  `B` → `C`) already drained correctly in the documented FIFO order; now has a permanent
  regression test (`tests/cases/async_self_queue_chain.cuff`).

## 34. `NativeFunctions.h` split into `engine/dlc/`

Every DLC's implementation used to live together in one 1,748-line
`engine/interpreter/NativeFunctions.h`. Split into `engine/dlc/`, one file per
library (`MathDLC.h`, `StringDLC.h`, `TimeDLC.h`, `RandomDLC.h`, `ListDLC.h`, `MapDLC.h`,
`ConvertDLC.h`, `NetworkDLC.h`, `FilesystemDLC.h`, `JsonDLC.h`), plus `DLCCommon.h` for
what several of them share: the `expectArgCount`/`expectNumber`/`expectStr`/`expectList`/
`expectMap`/`expectWhole` argument-checking helpers, `ensureStringSize`/`ensureItemCount`,
and the `textutil` UTF-8 namespace (used by both `StringDLC.h` and `JsonDLC.h`).
`NativeFunctions.h` itself is now ~100 lines: the always-on core builtins
(`registerBuiltins` — `print`/`input`/`type_of`, no `use` needed) and `registerDLC()`,
the single dispatcher every `use DLC:name` calls into, which now just `#include`s all
eleven files. `nativeLength`/`nativeContains`/`nativeIndexOf` (the three polymorphic
functions shared across str/list/map, see 9번/31번) live in `DLCCommon.h` rather than
being duplicated per file, for the same reason they were never split by library to begin
with. Every new file compiles standalone (checked by hand, one `#include "X.h"` +
empty `main()` per file) rather than silently depending on include order from whatever
happened to be pulled in first — the kind of hidden coupling splitting a file is supposed
to remove, not just relocate. Purely a file-organization change: no declaration moved
namespace, gained a new name, or changed behavior, and the full suite (unit tests,
`tests/cases`, `tests/errors`, `examples`) passes unchanged before and after.
