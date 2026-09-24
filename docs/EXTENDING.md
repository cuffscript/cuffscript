# 기능 추가 가이드 (Extending the Engine)

이 문서는 엔진에 새 기능을 추가할 때 "어느 파일을 건드려야 하는가"를 빠르게 찾기 위한
안내입니다. 각 항목은 실제로 동작을 검증한 최소 절차입니다.

## 1. 새로운 내장 함수 추가하기 (예: `print`, `sqrt`)

`engine/interpreter/NativeFunctions.h`에 함수를 추가합니다. 시그니처는 항상
`Value(std::vector<Value>& args, const SourceLocation& loc)`입니다.

```cpp
reg["my_func"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
{
    expectArgCount("my_func", args, 1, loc);
    double n = expectNumber("my_func", args, 0, loc);
    return Value::makeNumber(n * 2);
};
```

- 항상 존재해야 하면 `registerBuiltins()`에 추가하세요.
- 특정 `use DLC:이름`으로만 활성화되어야 하면 새 `registerXxxDLC()` 함수를 만들고
  `registerDLC()`의 분기에 추가하세요 (기존 라이브러리 이름과 충돌하지 않는지 확인).
- `expectArgCount`/`expectArgRange`/`expectNumber`/`expectStr`는 인자 검증과 함께
  일관된 `ArgumentError`/`TypeError` 메시지를 만들어 줍니다 — 새 함수도 이걸 재사용하세요.
  정수 인자는 `expectWhole`(±2^53 범위 검사 포함)을 쓰세요.
- 문자열이나 리스트를 크게 만들 수 있는 함수는 결과를 만들기 전에 `ensureStringSize`/
  `ensureItemCount`로 크기를 확인하세요 (한도는 `engine/common/Limits.h`).

## 2. 새로운 값 타입 추가하기

`engine/interpreter/Value.h`의 `Value` 클래스를 확장합니다.

1. `ValueType`에 새 항목을 추가하고 `valueTypeName()`에 이름을 추가합니다.
2. `Value::Storage` variant에 저장 타입을 추가하고, `make*`/`as*`/`is*` 헬퍼를 추가합니다.
3. `truthy()`, `appendDisplay()`, `strictEquals()`(`equalsImpl`/`scalarEquals`)의 switch에 새 case를 추가합니다
   (컴파일러가 `-Wswitch`로 누락된 case를 잡아 줍니다).

## 3. 새로운 문(statement) 또는 표현식(expression) 추가하기

1. `engine/parser/ASTNodes.h`: 새 구조체를 정의하고, `StmtKind`/`ExprKind`와 해당
   `variant`에 추가합니다.
2. 적절한 파서 파일에 파싱 로직을 추가합니다 (`StatementParser.h`가 문의 첫 토큰으로
   분기하고, `LiteralParser::parsePrimary`가 표현식의 첫 토큰으로 분기합니다). 새로운
   예약어가 필요하면 `engine/common/TokenTypes.h`와
   `engine/lexer/KeywordClassifier.h`에 추가하세요.
3. `engine/debug/ASTPrinter.h`에 출력 case를 추가합니다 (`--ast`로 확인 가능).
4. `engine/interpreter/Interpreter.h`의 `execStatement`/`evalExpr` switch에 실행 로직을
   추가합니다.

두 파서 클래스가 서로를 호출해야 하는 경우(예: 새 표현식이 하위 표현식을 파싱해야
하는 경우), `RegexExprParser.h`가 쓰는 패턴을 그대로 따르세요: 헤더에는 전방 선언 +
멤버 함수 **선언만** 두고, 실제 정의는 `ExpressionParser.h` 맨 아래 "Deferred
implementations" 섹션에 (양쪽 클래스가 모두 완전한 타입이 된 뒤에) 작성합니다.

## 4. 새로운 오류 종류 추가하기

1. `engine/common/ErrorCodes.h`의 `ErrorCode`에 알맞은 숫자대(1000=lexical,
   2000=syntax, 3000/3100=regex syntax/runtime, 4000=runtime, 5000=module,
   6000=resource limit(`or_else`로 잡히지 않음), 9000=internal) 안에서 새 값을 추가합니다.
2. 필요하면 `engine/common/CuffError.h`에 작은 서브클래스를 추가합니다 (기존 클래스
   중 하나로 충분하면 이 단계는 생략 가능 — 예: `CuffRuntimeError(ErrorCode::내코드, ...)`
   를 직접 던져도 됩니다).
3. `recoverable` 여부는 코드의 숫자대에서 자동으로 결정됩니다
   (`errorCodeRecoverable()`) — 3100~5999는 `or_else`가 잡을 수 있고, 그 외는 잡을 수
   없습니다.

## 5. 값 타입에 "얼릴 수 있는" 상태 추가하기 (예: `constant list`의 `isConstant`)

새 값 타입을 만들지 않고 기존 타입에 소수의 플래그만 추가해 동작을 바꾸는 패턴입니다
(v2.0.0의 `constant list` 참고).

1. 플래그는 해당 C++ 구조체/클래스에 직접 둡니다 (`ValueList::isConstant`) — `Value`
   자체나 별도 래퍼에 두지 않습니다. 리스트/맵은 참조 타입(`shared_ptr`)이라, 플래그가
   값 객체에 붙어 있어야 별명(alias)이나 함수 인자로 넘어가도 따라갑니다.
2. 그 상태를 처음 만드는 지점(선언 등)에서, 기존 값을 그대로 재사용하지 말고 **새 객체로
   복사**한 뒤 플래그를 설정하세요. 안 그러면 이미 다른 변수가 참조 중인 객체를 몰래
   얼려버릴 수 있습니다 (`Interpreter::freezeList` 참고).
3. 그 상태를 확인해야 하는 지점은 값을 "직접 변경하는" 곳들입니다 — 변수 재대입이
   아니라 내용 변경(`add`/`remove`/`change x[i]`) 쪽입니다. 이미 있는 변수-단위 상수
   체크(`Environment`의 `constants_`)와는 별개로, 값 자체의 플래그를 확인하는 코드를
   해당 연산의 실행부에 추가하세요.
4. 중첩된 원소까지 얼릴지(깊은 불변성) 말지(얕은 불변성)를 먼저 정하세요. `constant list`는
   파이썬 튜플처럼 얕은 불변성이라, 최상위 슬롯만 잠그고 내부에 든 리스트/맵은 그대로
   둡니다 — 새 타입에서는 이 부분을 다르게 선택해도 됩니다.

## 6. 함수 실행 중 상태 하나를 스코프에 맞춰 토글하기 (예: `pure`)

`async`/`returnable`/`pure`처럼 "이 함수 호출 동안만 유효한" 수식어를 추가하는 패턴입니다.

1. `FunctionDecl`에 불리언 필드를 추가하고 파서(주로 `FunctionParser.h`의 수식어
   반복문)에서 채웁니다. `DeclarationParser::isFunctionDecl`의 미리보기 조건에도 새
   키워드를 추가해야 `set 새키워드 func ...`가 함수 선언으로 인식됩니다.
2. `Interpreter`에 `bool current제어_ = false;` 멤버를 추가하고, `FrameGuard`가 호출
   진입/종료 시 이전 값을 저장했다가 복원하도록 합니다 (재귀 호출에서도 각 프레임이
   자기 자신의 값을 갖도록). 이 상태는 `Environment`가 아니라 `Interpreter`가 갖습니다 —
   같은 이유로 `returnable`도 이렇게 되어 있습니다.
3. 실제 제약/동작은 이 플래그를 확인하는 지점에 넣습니다. `pure`는 이름을 찾는 세 곳
   (`evalExpr`의 식별자 조회, `execChange`, `execCollectionOp`)에서 `look.owner ==
&globalEnv_`인지 확인하는 방식으로 구현했습니다 — 새 제약이 다른 조건이라면 그에 맞는
   지점을 고르세요.
4. 이 플래그는 **그 함수 자신의 본문에만** 적용되고, 호출된 다른 함수로는 전파되지
   않습니다 (그 함수가 호출을 마치면 `FrameGuard`가 자동으로 이전 값을 복원하기 때문).
   호출 그래프 전체에 전파되는 제약이 필요하다면 별도로 설계해야 합니다.

## 7. 정규식 패턴에 새 토큰 추가하기 (예: `[새토큰]`)

1. `engine/regex/RegexAst.h`: 필요하면 새 `RNodeKind`나 새 문자 클래스 판별 함수를
   추가합니다.
2. `engine/regex/RegexParser.h`의 `parseBracket()`에 `body == "새토큰"` 분기를
   추가합니다.
3. 가변 길이 패턴(이메일/전화번호/URL처럼)이면 `RegexAst.h`의 `PresetKind`에 추가하고
   `RegexMatcher.h`의 `presetLengths()`에 매칭 로직을 추가합니다. 고정 길이 문자
   판별이면 `RegexParser.h`의 이름 있는 클래스들처럼 `std::function<bool(unsigned
char)>`만 있으면 됩니다.
4. `/tmp` 등에서 `engine/regex/RegexEngine.h`만 단독으로 include하는 작은 테스트
   프로그램을 만들어 새 토큰을 검증하세요 (이 저장소를 만들 때 실제로 사용한 방법이며,
   전체 언어 파이프라인 없이 정규식 엔진만 빠르게 확인할 수 있습니다).

## 8. 검증 방법

새 기능을 추가한 뒤에는 최소한 다음을 확인하세요.

```bash
make clean && make        # -Wall -Wextra -Werror 이므로 경고가 곧 실패입니다
./cuffc --ast your_test.cuff   # 파싱 결과(AST)를 눈으로 확인
./cuffc your_test.cuff         # 실제 실행 결과 확인
bash tests/run.sh              # 전체 회귀 테스트 (기존 기능이 안 깨졌는지)
```

새 기능이면 `tests/cases/`(성공 케이스)나 `tests/errors/`(에러 케이스)에 테스트를 하나
같이 추가하세요 — `tests/README.md` 참고. 나중에 리팩토링할 때 이 테스트가 그대로 안전망이
됩니다.
