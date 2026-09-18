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
  (`set async returnable function ...`). 명세 예제에는 등장하지 않지만 문법상 자연스러운
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

## 7. `loop match`의 의미

`SPEC.md`의 설명("Python의 if 문 안에 for 문을 넣은 것과 같은 효과")은 다소
비유적입니다. 이 엔진은 `loop match 대상 is/IS 상태 do: ... end`를 `loop while`과
동일하게, 매 반복마다 조건을 다시 검사하는 것으로 구현했습니다.

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

| 라이브러리 | 제공 함수 |
|---|---|
| `DLC:math` | `sqrt`, `abs`, `pow`, `round`, `floor`, `ceil`, `min`, `max` |
| `DLC:string` | `upper`, `lower`, `trim`, `length`, `contains`, `starts_with`, `ends_with` |
| `DLC:time` | `now`, `timestamp` |
| `DLC:random` | `random`, `random_int` |
| `DLC:list` | `sort`, `reverse`, `join`, `unique` — 전부 원본을 바꾸지 않고 새 값을 반환 |
| `DLC:convert` | `to_number`, `to_str`, `to_boolean` — 명시적 타입 변환 |
| `DLC:json` | `to_json`, `from_json` — 아래 19번 항목 참고 |
| `DLC:network` | `fetch`, `get`, `post` — **불러오기는 항상 성공**하지만, 실제로 호출하면 이 실행 환경에 네트워크 샌드박싱이 없다는 명확한 `ModuleError`를 던집니다. |

`DLC:list`/`DLC:convert`를 추가하며 발견한 것: 라이브러리 이름이 `list`, `count`,
`find`처럼 언어 예약어와 겹치면 `use DLC:list`가 파싱조차 안 되는 버그가 있었습니다
(`ImportParser`가 `IDENTIFIER` 토큰만 이름으로 인정했기 때문). 지금은 IDENTIFIER든
예약어든 "글자로 이루어진 토큰"이면 모두 이름으로 인정하도록 고쳤습니다 — 앞으로
어떤 DLC/모듈 이름을 추가해도 이 문제가 재발하지 않습니다.

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
(E3103)를 깔끔하게 던지는 것을 확인했습니다. 교훈: "충분히 보수적"이라고 생각한 숫자도
실측 없이는 믿으면 안 됩니다.

**식별자로 예약어를 쓸 수 없는 범위가 생각보다 넓음 (미해결, 기록만 해둠).**
`set number add to 5`, `set returnable function add(...) do:` 둘 다 파싱 에러가 납니다 —
`add`/`count`/`find`/`split`/`replace`/`match`/`in`/`by`/`not`/`global` 등, 문법 키워드로
쓰이는 흔한 단어들을 변수명/함수명으로 전혀 쓸 수 없습니다. `ImportParser`에서 DLC 이름이
같은 문제였던 것과 동일한 원인(`TokenType::IDENTIFIER`만 엄격히 검사)인데, 이번엔
`DeclarationParser`/`FunctionParser` 등 이름을 선언하는 모든 지점에 퍼져있어서 범위가 더
큽니다. 아직 고치지 않았습니다 — `tests/errors/argument_count_mismatch.cuff`가 원래
`add`라는 함수명을 쓰려다 이 문제에 걸려서 `combine`으로 우회했습니다.

## 17. Regex matching is codepoint-based (not byte-based)

*(Notes from here on are written in English.)*

The matcher walks the subject string one **UTF-8 codepoint** at a time, not one byte.
Positions are still stored as byte offsets internally — that keeps `substr()` on captures
free and avoids building an index table per match — but every place that *advances* a
position now consumes a whole codepoint (`engine/common/Utf8.h`).

What this changes, concretely:

- `[any]` matches one codepoint. `"안녕하세요" is "[any]5"` is now true (it was false
  before, because the string is 15 bytes).
- A literal non-ASCII character written directly in a pattern (`"안녕"`) is compiled into a
  single multi-byte literal node and compared as one unit, instead of byte-by-byte.
- Negated sets (`[!num]`, `[!a-z]`) match a non-ASCII codepoint. This is the one case where
  a named class *can* apply to multi-byte text, and it's handled by testing the whole
  codepoint rather than each byte.
- `search()` only ever starts an attempt at a codepoint boundary, and `[one:...]`
  alternatives must also *end* on one — otherwise a match could span a partial character
  and `substr()` would produce mojibake.
- `[edge]` treats any non-ASCII codepoint as a word character, so `[edge]안녕[edge]` works.
  This follows the spec's own definition (REGEX.md section 17: the boundary between a word
  and whitespace/punctuation/string-start/end).

What deliberately stays ASCII-only, per the spec's explicit wording:

- `[let]` (영문 알파벳 / English alphabet), `[low]`, `[up]`, `[str]` (영문자 + 숫자),
  `[word]` (영문자 + 숫자 + 언더바 — an *identifier* class), `[num]`, `[hex]`, and the
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
with `FractionalIndex` (E4024) instead of silently rounding it, which is what the engine
used to do. A computed index that lands on `2.5` almost always means the *calculation* is
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
- **`ArgumentError` means the wrong *number* of arguments; `ValueError`
  (`InvalidArgumentValue`, E4025) means the right number but a value the function can't
  use** — `sqrt(-1)`, `to_number("abc")`, malformed `from_json` input, `random_int(5, 1)`.
  These previously all reported as E4010 `ArgumentCountMismatch`, which was simply the wrong
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

| benchmark | before | after |
| :--- | ---: | ---: |
| `fib(27)` (635k calls) | 0.241s | 0.140s |
| 2M-iteration loop with `change` | 0.287s | 0.173s |
| 400 globals, 200k lookups | 0.059s | 0.027s |
| 3-argument call overhead | 252ns | ~145ns |

**What would come next, and why it hasn't been done.** The profile is now dominated by
`evalExpr` itself — AST node dispatch and `Value` copies — which is the intrinsic cost of a
tree-walking interpreter. Getting substantially past this means compiling to bytecode and
running a stack VM, which is a rewrite of the execution core rather than a refactor of it.
That is a reasonable next step if a real workload demands it; it is not worth the risk on
speculation, and the current engine is in the same performance range as CPython on
call-heavy code.
