<p align="center">
    <img src="https://raw.githubusercontent.com/cuffscript/cuffscript/refs/heads/main/assets/cuffscript_horiz.svg" alt="CuffScript 로고" width="360" />
</p>

---

# CuffScript 프로그래밍 언어

CuffScript는 자연어 키워드와 간결한 문법을 사용하는 스크립트 언어입니다. 이 저장소는 토크나이저, 렉서, 파서, 자체 정규식 엔진, 그리고 실제로 CuffScript 프로그램을 실행하는 트리 워킹(tree-walking) 인터프리터로 이루어진 완전한 엔진을 담고 있습니다.

## 현재 상태

엔진은 [docs/SPEC.md](docs/SPEC.md)와 [docs/REGEX.md](docs/REGEX.md)에 정의된 전체 파이프라인을 구현합니다.

```text
CuffScript source
    -> Tokenizer
    -> Lexer
    -> Parser  -> AST
    -> Interpreter (AST를 실행)
```

기본적으로 `./cuffc program.cuff`는 프로그램을 **실행**합니다. `--ast`를 넘기면 실행 대신 토크나이저/렉서/파서 단계 결과만 출력합니다 (엔진 자체를 개발/디버깅할 때 유용).

모든 단계(어휘, 문법, 패턴, 런타임, 모듈)의 오류는 하나의 체계적으로 코드화된 예외 계층을 통해 발생합니다 — 아래 [오류 처리](#오류-처리) 참고 — 그래서 실패 상황이 일관되고, 새 오류 종류를 추가하기도 쉽습니다.

## 문법 개요

### 변수 선언과 변경

```cuff
set number age to 25
set str name to "Alice"
set list colors to ["red", "green", "blue"]

change age to 26
```

상수는 `constant`를 사용하며 이름은 반드시 전체 대문자여야 합니다 (런타임에 검사됩니다 — 소문자를 하나라도 쓰거나, 이후 `change`로 값을 바꾸려 하면 즉시 런타임 오류가 발생합니다).

```cuff
set constant number MAX_LEVEL to 99
```

함수 안에서 함수 바깥에 선언된 변수를 (읽기뿐 아니라) 수정하려면, 먼저 `change 변수명 to global`로 한 번 연결해야 합니다.

```cuff
set number counter to 0
set function increment() do:
    change counter to global
    change counter to counter + 1
end
```

### 조건문과 반복문

제어문은 `do:`로 실행부를 시작하고 `end`로 닫습니다. 한 줄 축약형과 여러 줄 블록형을 사용할 수 있으며, 여러 줄 블록은 들여쓰기를 사용합니다.

```cuff
if score >= 90 do: print("excellent") end

loop repeat i to 1 ~ 3 do:
    print(i)
end
```

지원되는 반복문 형태는 `loop repeat`, `loop while`, `loop match`입니다. 반복문 안에서는 `stop`으로 가장 가까운 반복문을 종료합니다.

### 표현식과 컬렉션

- 비교: `is`(대소문자 구분), `IS`(대소문자 무시); `is not` / `IS not`으로 부정 가능
- 논리 부정: `!` — 비교 연산자보다 **낮은** 우선순위로 묶입니다. 즉 `!lvl is MAX_LEVEL`은 `!(lvl is MAX_LEVEL)`을 의미합니다.
- 리스트와 맵: `[]`, `{}` (맵은 입력 순서를 유지합니다)
- 1부터 시작하는 인덱스와 포함 범위 슬라이싱: `[1]`, `[2~4]`, 음수 인덱스는 끝에서부터 셉니다 (`[-1]`은 마지막 요소)
- f-string: `f"Hello, {name}"` — `{...}` **내부**의 문자열 리터럴은 `'홑따옴표'`를 사용하세요. `"`를 쓰면 f-string이 그 지점에서 먼저 닫혀버립니다. `{{`/`}}`는 중괄호 문자 그대로를 출력합니다.
- 컬렉션 조작: `add`, `remove`, `replace`, 또는 `change`를 이용한 인덱스 대입

```cuff
set list items to ["sword", "shield"]
add "potion" to items
change items[1] to "magic_staff"
remove 2 from items
```

### 패턴 매칭 (커스텀 정규식 방언)

CuffScript의 패턴 문법에는 `\d`, `\w`, `^`, `$` 같은 것이 없습니다 — 모든 토큰은 일반 문자이거나 `[num]`, `<name:...>`, `[one:a|b|c]` 같은 대괄호/꺾쇠 단어입니다. 자세한 내용은 [docs/REGEX.md](docs/REGEX.md)를 참고하세요.

```cuff
if email is "[str]+@[str]2~10" do: print("이메일 형태처럼 보입니다") end

set match parsed to match log_line from "<date:[num]4-[num]2-[num]2> <msg:[any]+>"
if parsed is not empty do:
    print(f"date={parsed['date']} msg={parsed['msg']}")
end

set list codes to find "T-[num]3" from article g
set str masked to replace "[num]4-[num]4" in phone to "****-****"
set list parts to split "a,b,c" by ","
set number n to count "[num]+" in text
```

패턴 매칭은 파국적 백트래킹(catastrophic backtracking)으로부터 스텝 수 제한과 시간 제한으로 보호되며, 한도를 넘으면 (멈추는 대신) 복구 가능한 `RegexRuntimeError`를 발생시킵니다.

### 함수와 모듈

함수 선언은 `returnable`과 `async` 수식어를 자유롭게 조합할 수 있습니다 (`set function`, `set returnable function`, `set async function`, `set async returnable function` 등). `async` 함수를 호출하려면 `await`가 필요하며, 이 엔진에서는 완료까지 동기적으로 실행됩니다.

```cuff
set returnable function double(value) do:
    return value * 2
end

set number result to double(21)
```

`use DLC:<이름>`은 내장 라이브러리(`math`, `string`, `time`, `random`, `network`)를 불러옵니다 — 전체 함수 목록은 [docs/IMPLEMENTATION_NOTES.md](docs/IMPLEMENTATION_NOTES.md) 참고. `use <이름> from <경로>`는 실행 중인 스크립트를 기준으로 다른 `.cuff` 파일을 불러와 최상위 함수/변수를 현재 스코프에 합칩니다.

오류 처리 결합 구문은 `or_else do: ... end`이며, 바로 앞 구문에서 발생한 (문법 오류가 아닌) 복구 가능한 런타임 오류를 잡아냅니다.

```cuff
set number result to risky_call() or_else do:
    change result to -1
end
```

## 오류 처리

엔진이 발생시키는 모든 오류는 `CuffError`(`engine/common/CuffError.h`)를 상속하며, 고유한 숫자 코드(`engine/common/ErrorCodes.h`), 분류, 소스 위치, 메시지, 그리고 선택적인 힌트를 담고 있습니다. 예:

```text
[E4008] Runtime Error at line 12, column 5: index 10 is out of range (length 3)
    hint: use 1 for the first element, or -1 for the last
```

코드는 범위별로 묶여 있습니다 (1000번대 어휘, 2000번대 문법, 3000번대 패턴 문법/런타임, 4000번대 인터프리터 런타임, 5000번대 모듈, 9000번대 내부). Runtime/Regex-runtime/Module 오류만 `recoverable`(복구 가능)이며, 이것이 정확히 `or_else`가 잡을 수 있는 오류들입니다. 잘못된 프로그램 자체를 나타내는 오류(어휘/문법/패턴 문법 오류)는 절대 복구 가능하지 않습니다. 새로운 오류 종류를 추가하는 것은 순전히 덧붙이는 작업입니다 — `ErrorCodes.h`에 코드를 추가하고, 필요하면 `CuffError.h`에 작은 서브클래스를 추가하면 끝입니다.

## 디렉터리 구조

```text
engine/
├── common/       공통 타입, 토큰, 체계적인 오류 계층, 소스 위치
├── tokenizer/    문자열을 원시 토큰으로 변환
├── lexer/        키워드 분류와 콜론 규칙 검증
├── parser/       표현식, 선언, 제어문, 함수, 모듈 파싱
├── regex/        CuffScript 자체 패턴 컴파일러 + 백트래킹 매처 (docs/REGEX.md)
├── interpreter/  트리 워킹 인터프리터: Value 모델, 스코프, 실행
└── debug/        토큰/AST 출력 (--ast에서 사용)
```

주요 진입점은 `engine/CuffEngine.h`의 `CuffEngine::execute`(파싱+실행)와 `CuffEngine::run`(파싱만, `--ast`용)입니다. 언어 규칙의 상세 내용은 [docs/SPEC.md](docs/SPEC.md)와 [docs/REGEX.md](docs/REGEX.md)를, 명세가 침묵하는 부분에 대한 구현 결정(비동기 실행 모델, DLC 함수 등)은 [docs/IMPLEMENTATION_NOTES.md](docs/IMPLEMENTATION_NOTES.md)를 참고하세요.

## 빌드

### Makefile 사용

C++17 컴파일러가 설치된 환경에서 저장소 루트에서 실행합니다.

```bash
make
```

생성되는 실행 파일 이름은 `cuffc`입니다.

### Visual Studio

Visual Studio에서 `Desktop development with C++` 워크로드를 설치한 뒤 빈 C++ 프로젝트를 만들고 `main.cpp`와 `engine/` 아래의 헤더 파일을 추가합니다. 프로젝트의 C++ 표준은 C++17로 설정합니다.

## 실행

스크립트 실행:

```bash
./cuffc path/to/program.cuff
```

또는 표준 입력으로 소스 전달:

```bash
echo 'print("Hello, CuffScript!")' | ./cuffc
```

실행 대신 토큰+AST만 출력 (개발/디버깅용):

```bash
./cuffc --ast path/to/program.cuff
```

성공하면 프로그램의 출력이 표준 출력에 나타나고 종료 코드는 0입니다. 오류(어휘, 문법, 패턴, 런타임, 모듈 중 어떤 것이든)가 발생하면 표준 오류에 형식화된 오류 한 줄이 출력되고 종료 코드는 1입니다.

## 명세

공식 언어 명세는 [docs/SPEC.md](docs/SPEC.md)에, 패턴 매칭 방언은 [docs/REGEX.md](docs/REGEX.md)에 있습니다. 두 문서는 다음을 다룹니다.

- `set`, `change`, `constant`를 이용한 선언 규칙
- 콜론 공백 규칙과 `note` / `endnote` 주석
- 비교, 부정, 인덱싱, 슬라이싱, 커스텀 정규식 방언
- 리스트와 맵 조작
- 조건문, 반복문, 함수, `await`, `or_else`
- 입력, 출력, 모듈 로드 문법

명세가 런타임 동작을 정의하지 않고 남겨둔 부분(`async`/`await`가 대표적입니다 — 명세 스스로 "별도의 구현 명세에서 정의합니다"라고 미뤄두고 있습니다)에 대해서는, 이 엔진이 어떤 선택을 했고 왜 그랬는지를 [docs/IMPLEMENTATION_NOTES.md](docs/IMPLEMENTATION_NOTES.md)에 정리해 두었습니다.

---
