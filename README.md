<p align="center">
    <img src="https://raw.githubusercontent.com/cuffscript/cuffscript/refs/heads/main/assets/cuffscript_horiz.svg" alt="CuffScript logo" width="360" />
</p>

---

# The CuffScript programming language

CuffScript는 자연어 키워드와 간결한 문법을 사용하는 스크립트 언어입니다. 현재 저장소의 엔진은 CuffScript 소스 코드를 토큰화하고, 키워드를 분류한 뒤, 추상 구문 트리(AST)로 변환하는 프론트엔드 파이프라인을 제공합니다.

## 현재 상태

현재 구현의 처리 흐름은 다음과 같습니다.

```text
CuffScript source
    -> Tokenizer
    -> Lexer
    -> Parser
    -> AST
```

`main.cpp`는 파일 경로를 인자로 받거나 표준 입력에서 소스 코드를 읽습니다. 처리에 성공하면 토큰화 결과, 렉서 결과, AST를 출력하고, 실패하면 오류를 출력합니다.

이 저장소의 `engine/`는 주로 언어 프론트엔드 구현으로 구성되어 있습니다. 명세에 정의된 실행 문법이 모두 런타임에서 실행된다는 의미는 아니며, 현재 진입점도 AST 생성 결과를 출력하는 단계까지 담당합니다.

## 문법 개요

### 변수 선언과 변경

```cuff
set number age to 25
set str name to "Alice"
set list colors to ["red", "green", "blue"]

change age to 26
```

상수는 `constant`를 사용하며 이름은 대문자와 밑줄 조합을 사용합니다.

```cuff
set constant number MAX_LEVEL to 99
```

### 조건문과 반복문

제어문은 `do:`로 실행부를 시작하고 `end`로 닫습니다. 한 줄 축약형과 여러 줄 블록형을 사용할 수 있으며, 여러 줄 블록은 들여쓰기를 사용합니다.

```cuff
if score >= 90 do: print("excellent") end

loop repeat i to 1 ~ 3 do:
    print(i)
end
```

지원하도록 정의된 반복문 형태는 `loop repeat`, `loop while`, `loop match`입니다. 반복문 안에서는 `stop`으로 가장 가까운 반복문을 종료합니다.

### 표현식과 컬렉션

- 비교: `is`, 대소문자 무시 문자열 비교: `IS`
- 부정: `!`
- 리스트와 맵: `[]`, `{}`
- 1부터 시작하는 인덱스와 포함 범위 슬라이싱: `[1]`, `[2~4]`
- f-string: `f"Hello, {name}"`
- 컬렉션 조작: `add`, `remove`, `replace`

```cuff
set list items to ["sword", "shield"]
add "potion" to items
replace items[1] to "magic_staff"
remove 2 from items
```

### 함수와 모듈

함수 선언에는 `function`, `returnable`, `async`를 사용할 수 있습니다. 모듈은 `use`와 `from`으로 불러오도록 명세되어 있습니다.

```cuff
set returnable function double(value) do:
    return value * 2
end

set number result to double(21)
```

오류 처리 결합 구문은 `or_else do: ... end`로 정의되어 있습니다.

## 디렉터리 구조

```text
engine/
├── common/       공통 타입, 토큰, 오류, 소스 위치
├── debug/        토큰 및 AST 출력
├── lexer/        키워드 분류와 콜론 규칙 검증
├── parser/       표현식, 선언, 제어문, 함수 및 모듈 파싱
└── tokenizer/    문자열을 원시 토큰으로 변환
```

주요 공개 진입점은 `engine/CuffEngine.h`의 `CuffEngine::run`입니다. 언어 규칙의 상세 내용은 [docs/SPEC.md](docs/SPEC.md)를 참고하세요.

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

파일을 인자로 전달할 수 있습니다.

```bash
./cuffc path/to/program.cuff
```

또는 표준 입력을 사용할 수 있습니다.

```bash
echo 'print("Hello, CuffScript!")' | ./cuffc
```

실행이 성공하면 토큰과 AST를 출력합니다. 문법 오류가 발생하면 오류 메시지를 출력하고 실패 코드로 종료합니다.

## 명세

언어의 공식 중간 명세는 [docs/SPEC.md](docs/SPEC.md)에 있습니다. 명세에는 다음 내용이 포함되어 있습니다.

- `set`, `change`, `constant`를 이용한 선언 규칙
- 콜론 공백 규칙과 `note` / `endnote` 주석
- 비교, 부정, 인덱싱, 슬라이싱, 정규식 단축형
- 리스트와 맵 조작
- 조건문, 반복문, 함수, `await`, `or_else`
- 입력, 출력, 모듈 로드 문법
