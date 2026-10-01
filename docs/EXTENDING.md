# 기능 추가 가이드 (Extending the Engine)

이 문서는 엔진에 새 기능을 추가할 때 "어느 파일을 건드려야 하는가"를 빠르게 찾기 위한
안내입니다. 각 항목은 실제로 동작을 검증한 최소 절차입니다.

## 1. 새로운 내장 함수 추가하기 (예: `print`, `math_sqrt`)

항상 있는 내장 함수는 `engine/interpreter/NativeFunctions.h`의 `registerBuiltins()`에,
`use DLC:이름`으로만 활성화되는 라이브러리 함수는 `engine/dlc/` 아래 그 라이브러리의
파일(`MathDLC.h`, `StringDLC.h`, `FilesystemDLC.h`, ...)에 추가합니다. 여러 라이브러리가
공유하는 인자 검증 헬퍼(`expectArgCount` 등)와 UTF-8 텍스트 유틸은 `engine/dlc/DLCCommon.h`에
있습니다. 시그니처는 항상 `Value(std::vector<Value>& args, const SourceLocation& loc)`입니다.

```cpp
reg["my_func"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
{
    expectArgCount("my_func", args, 1, loc);
    double n = expectNumber("my_func", args, 0, loc);
    return Value::makeNumber(n * 2);
};
```

- 항상 존재해야 하면 `registerBuiltins()`에 추가하세요.
- 완전히 새로운 라이브러리라면 `engine/dlc/`에 새 파일(`DLCCommon.h`만 include)을 만들고,
  `registerXxxDLC()` 함수를 작성한 뒤, `NativeFunctions.h`에 그 파일을 `#include`하고
  `registerDLC()`의 분기에 추가하세요 (기존 라이브러리 이름과 충돌하지 않는지 확인).
- DLC 함수 이름은 `라이브러리_동사` 형태(`math_sqrt`, `str_upper`, `list_sort`,
  `file_read`, ...)로 짓습니다 — 어느 라이브러리 소속인지 이름만으로 알 수 있고, 모든
  라이브러리가 하나의 `natives_` 맵을 공유하므로 다른 라이브러리와 이름이 겹칠 위험도
  줄어듭니다. 여러 자료형에 걸쳐 의도적으로 같은 구현을 공유하는 다형 함수
  (`length`/`contains`/`index_of`)만 예외입니다.
- 파일 시스템에 접근하는 DLC 함수는 경로를 직접 열지 말고 `resolveSandboxedPath()`
  (`engine/dlc/FilesystemDLC.h`, 내부적으로 `engine/common/PathSandbox.h`의 `isInsideRoot()` 사용)를
  거치게 하세요 — 모듈 로딩과 `DLC:filesystem`이 같은 샌드박스 루트를 공유합니다.
- 호스트가 통째로 끌 수 있어야 하는 위험한 라이브러리(네트워크/파일 시스템처럼)는
  `NetworkDLCOptions`/`FilesystemDLCOptions`와 같은 패턴으로 옵션 구조체를 만들고,
  `CuffEngine::Options` → `Interpreter::Config` → `execUse()` → `registerDLC()`로 값을
  전달하세요 (CLI 플래그는 `main.cpp`).
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
   (컴파일러가 `-Wswitch`로 누락된 case를 잡아 줍니다). `engine/dlc/JsonDLC.h`의
   `jsonStringifyInto()`처럼 `ValueType`으로 분기하는 다른 switch도 마찬가지입니다.


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
4. 이 플래그는 기본적으로 **그 함수 자신의 본문에만** 적용되고, 호출된 다른 함수로는
   전파되지 않습니다 (그 함수가 호출을 마치면 `FrameGuard`가 자동으로 이전 값을
   복원하기 때문). `pure`는 이 기본 동작으로는 우회가 너무 쉬워서(전역을 만지는 일반
   함수를 한 겹 감싸서 호출하면 그만) 호출 지점(`evalCall`/`invokeAwaited`)에서
   `checkPureCallAllowed()`로 "pure 함수는 순수하지 않은 사용자 함수를 호출할 수 없다"를
   추가로 강제합니다 — 호출 그래프 전체에 전파되는 제약이 필요하면 이 패턴을 따르세요.

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

## 9. 이름 자리에 예약어를 허용하기 (파서)

새로운 문장/선언 문법에서 "여기에 이름이 온다"는 자리를 만들 때는 `p.check(TokenType::IDENTIFIER)`
대신 `isWordLikeToken(p.current())`(`ParserCore.h`)를 쓰세요. 그러지 않으면 `add`, `count`,
`find`처럼 다른 곳에서 예약어로 쓰이는 단어가 이름으로 거부됩니다. 이름 자리는 문법상 "정확히
한 단어가 오고 그 다음 토큰이 정해져 있는" 고정 위치라서 예약어를 받아도 모호하지 않습니다.

표현식 위치에서 이름을 다시 읽어오는 경우(`LiteralParser::parsePrimary`)는 다릅니다 — 그
토큰 타입으로 "무엇을 파싱할지"를 결정하는 유일한 곳이라, 그 키워드가 표현식 문법에서 다른
뜻을 갖는지에 따라 처리가 갈립니다:
- 표현식 문법에서 아무 뜻도 없는 커넥터(`add`/`to`/`in`/`by`/`global`/`not`/`from`)는
  `isBareIdentifierKeyword()`에 추가하면 그대로 식별자가 됩니다.
- 이미 자기만의 표현식 구문이 있는 키워드(`match`/`find`/`replace`/`split`/`count`)는 한
  토큰 미리보기(`looksLikeConstructContinuation()`)로 "그 구문이 시작되는가 / 그냥 이름인가"를
  구분합니다 — 이때 `(`와 `[`는 일부러 뺍니다. 후위 파싱이 `이름(...)`을 호출로,
  `이름[...]`을 인덱싱으로 바꾸므로, 여기서 이 둘을 "구문 시작"으로 취급하면 그 이름의
  함수/변수를 호출하거나 인덱싱할 방법이 없어집니다.
- 블록/제어 흐름 키워드(`end`, `do`, `if`, ...)와 리터럴(`true`/`false`/`empty`)은 일부러
  예약어로 남겨 둡니다 — 이런 토큰이 표현식 자리에 나타난다는 건 대개 피연산자가 빠진
  진짜 문법 오류라서, 이름으로 받아주면 명확한 "unexpected token" 대신 엉뚱한 런타임
  에러로 바뀌기 때문입니다.

## 10. 에러 메시지의 소스 줄/캐럿 (`^`)

`CuffError`는 소스 텍스트를 모릅니다 — `what()`은 메시지와 힌트만 렌더링합니다. 사용자에게
보이는 최종 메시지의 소스 줄과 `^`는 `CuffEngine::execute()`/`run()`이 `CuffError`를 잡는
지점(원본 소스가 아직 스코프 안에 있는 곳)에서 `renderErrorWithSnippet()`으로 붙입니다. 새
에러를 던지는 코드는 정확한 `SourceLocation`(특히 `offset`, 바이트 오프셋)만 채워 주면 되고,
캐럿 표시는 자동입니다 — 캐럿의 가로 위치는 바이트가 아니라 코드포인트 단위로 계산되므로 같은
줄에 한글 등이 앞서 있어도 정렬이 맞습니다. 이 동작을 바꾸면 `tests/unit/error_snippet_test.cpp`가
알려 줍니다.

