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

## 2. 새로운 값 타입 추가하기

`engine/interpreter/Value.h`의 `Value` 클래스를 확장합니다.

1. `ValueType`에 새 항목을 추가하고 `valueTypeName()`에 이름을 추가합니다.
2. `Value::Storage` variant에 저장 타입을 추가하고, `make*`/`as*`/`is*` 헬퍼를 추가합니다.
3. `truthy()`, `toDisplayString()`, `strictEquals()`의 switch에 새 case를 추가합니다
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
   9000=internal) 안에서 새 값을 추가합니다.
2. 필요하면 `engine/common/CuffError.h`에 작은 서브클래스를 추가합니다 (기존 클래스
   중 하나로 충분하면 이 단계는 생략 가능 — 예: `CuffRuntimeError(ErrorCode::내코드, ...)`
   를 직접 던져도 됩니다).
3. `recoverable` 여부는 코드의 숫자대에서 자동으로 결정됩니다
   (`errorCodeRecoverable()`) — 3100~5999는 `or_else`가 잡을 수 있고, 그 외는 잡을 수
   없습니다.

## 5. 정규식 패턴에 새 토큰 추가하기 (예: `[새토큰]`)

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

## 6. 검증 방법

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
