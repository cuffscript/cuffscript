# CuffScript (Cuff) 공식 명세서 — 중간 명세

### 1\. 선언 및 변경 (set & change)

- **설명:** 데이터 최초 생성이나 메모리 공간 등록 시에는 무조건 `set [타입]` 구조를 사용합니다. 이때 값을 저장(대입)하는 기호는 자연어 키워드 `to`만 사용합니다. 기존에 선언된 변수의 값을 수정하거나 갈아 끼울 때는 선언부와 확실히 격리하기 위해 `change` 키워드를 사용하며, 대입에는 동일하게 `to`를 사용합니다.

- **문법:**
    - 변수 선언: `set [자료형] [변수명] to [값]`
    - 변수 변경: `change [변수명] to [값]`
- **예시:**

```cuff
set number age to 25
set str name to "Alice"
set empty data to empty  note: 값이 비어있음을 선언할 때는 empty 사용

change age to 26
change name to "Bob"
```

---

### 2\. 상수 선언 규칙 (set constant)

- **설명:** 한 번 지정하면 변경할 수 없는 상수를 선언할 때는 `set` 키워드 바로 뒤에 `constant`를 붙입니다. 상수의 대입 기호는 `=`를 허용하지 않으며 **오직 `to`만을 사용**해야 합니다.

    상수의 이름은 식별의 가독성과 안전성을 보장하기 위해 단어의 글자 수나 결합 개수와 전혀 상관없이 무조건 전체 대문자(UPPER*CASE)로 작성해야 합니다. 복합 단어를 연결하여 이름을 지을 때는 언더바(`*`) 기호를 허용합니다.

    이 명명 규칙을 위반하거나 상수를 대상으로 값을 변경하는 `change` 구문을 실행하면 인터프리터 엔진이 즉시 구동을 차단하고 런타임 에러를 발생시킵니다.

- **문법:** `set constant [자료형] [상수명] to [값]`

- **예시:**

```cuff
set constant number X to 10
set constant number PI to 3.14
set constant str API_URL to "https://cufflang.dev"
```

---

### 3\. 콜론(:) 공백 규격 및 주석 (note:, endnote)

- **설명:** 소스코드 파일의 정갈함을 언어 차원에서 강제하기 위해, 제어문 분기나 주석 등에 사용하는 모든 콜론(`:`) 기호에는 **콜론 앞 공백 절대 금지, 콜론 뒤 공백 허용(자유)**이라는 규격을 기계적으로 적용합니다.

    이를 위반하면 렉서(Lexer)가 토큰을 쪼개는 단계에서 즉시 문법 에러(Syntax Error)를 선언합니다.

    주석은 단일 줄 주석인 `note:`와 여러 줄 주석인 `note: ~ endnote` 구조를 제공합니다. 여러 줄 주석 블록 내부의 줄바꿈과 들여쓰기는 개발자가 자유롭게 쓸 수 있도록 완전히 보장됩니다.

- **문법:**
    - 단일 줄 주석: `note: [내용]`
    - 여러 줄 주석:
      `note: [줄바꿈] [자유로운 다중 줄 내용] [줄바꿈] endnote`
    - 구문 구분자: `[구문] do: [실행]` (`do:` 뒤의 실행부가 한 줄에서 완결되는 경우 해당 구문은 한 줄 축약형으로 취급하며 들여쓰기를 요구하지 않습니다. 실행부가 여러 줄로 확장되는 경우 블록형 구문으로 취급하며 각 실행문은 규정된 들여쓰기를 사용해야 합니다.)
- **예시:**

```cuff
note: 올바른 한 줄 주석 양식입니다.
if X is 10 do: print("통과") note: do: 기호 또한 앞 공백은 금지되고 뒤 공백은 허용됩니다.

note:
이곳은 여러 줄 주석 영역입니다.
앞의 들여쓰기 공간이나 줄바꿈 횟수에 전혀 제약을 받지 않으며 자유롭게 작성 가능합니다.
endnote
```

---

### 4\. 비교 및 부정 연산자 (is, IS, !)

- **설명:** 조건 검증을 위한 동등 비교 연산자는 알파벳 구성은 동일하나 대소문자 형태(`is` / `IS`)에 따라 내부 판정 엔진이 완전히 이원화됩니다.

    소문자 `is`는 일반 동등 비교 연산자로 사용됩니다. 영문 대소문자 및 데이터의 자료형 규격 등을 포함한 판정 방식은 CuffScript의 타입 규칙에 따릅니다.

    대문자 `IS`는 **영어 문자열 전용 비교 연산자**입니다. 영어 문자열을 검사할 때 영문 알파벳의 대소문자 구분을 완전히 소멸시키고 오직 글자의 알파벳 알맹이 내용이 같으면 참으로 판단하는 대소문자 무시(Case-Insensitive) 비교를 수행합니다.

    느낌표 기호(`!`)는 참을 거짓으로, 거짓을 참으로 반전시키는 글로벌 표준의 부정 연산자(NOT) 역할을 담당합니다.

    괄호가 사용된 경우에는 괄호 내부의 표현식이 우선적으로 평가됩니다. 괄호가 없는 복합 표현식의 해석 순서는 CuffScript의 연산자 우선순위 규칙에 따릅니다.

- **문법:**
    - `[값] is [값]`
    - `[영어 문자열] IS [영어 문자열]`
    - `![불리언값]`
- **예시:**

```cuff
set str input_text to "Apple"

if input_text is "apple" do: print("대소문자가 달라서 이 문장은 실행되지 않습니다.") end
if input_text IS "apple" do: print("대소문자를 무시하므로 이 문장은 정상 실행됩니다.") end

set boolean is_active to false

if !is_active do: print("false가 참(true)으로 반전되어 이 문장이 구동됩니다.") end
```

---

### 5\. 1-Based 인덱싱 및 물결 범위 슬라이싱 (~)

- **설명:** 자연어 직관을 반영하여 CuffScript 내의 연속형 자료구조(배열, 리스트, 문자열 등)의 시작 인덱스 번호는 **0이 아닌 1을 첫 번째 주소로 지정**합니다.

    CuffScript 런타임 시스템 내에 인덱스 0 주소는 정의되어 있지 않으며, 0에 접근을 시도하면 가상 머신이 즉시 경고 에러를 송출하고 정지합니다.

    마이너스(`-`) 부호가 결합된 인덱스는 데이터의 맨 뒤에서부터 거꾸로 순위를 매겨 역방향으로 접근하며, 맨 뒤의 첫 번째 칸은 `-1` 주소를 부여받습니다.

    데이터의 일부분을 잘라내는 슬라이싱 영역은 물결 기호 `~`를 대괄호 내부에 배치하여 수행하며, 시작 인덱스와 끝 인덱스 번호에 걸린 데이터를 모두 명확하게 포함(Inclusive)하여 추출합니다.

- **문법:**
    - `[컬렉션명][인덱스]`
    - `[컬렉션명][시작인덱스~끝인덱스]`
- **예시:**

```cuff
set list colors to ["red", "green", "blue", "yellow"]

print(colors[1])  note: 첫 번째 요소인 "red"가 화면에 출력됩니다. 0은 존재하지 않습니다.
print(colors[-1]) note: 역순 맨 뒷주소인 "yellow"가 출력됩니다.

set list sub_colors to colors[2~3]
print(sub_colors) note: 2번(green)과 3번(blue)을 모두 포함하여 ["green", "blue"]가 추출됩니다.
```

---

### 6\. JS급 초간결 정규식 및 타입 기반 단축형 매칭

- **설명:** 자바스크립트 수준의 정규식 연산 기능을 지원하면서도 복잡한 특수기호 조합(`^`, `$`, `\d`, `\w` 등)을 완전히 소멸시켰습니다.

    문자열 리터럴 텍스트 내부에 대괄호 형태로 가공된 데이터형 축약어(`[num]`, `[str]`)와 범위 물결 기호(`~`) 등을 섞어서 직관적인 패턴을 조립한 뒤, 대문자 `IS` 혹은 소문자 `is` 연산자와 결합하여 유효성을 검증합니다.

    세부적인 정규식 처리 규칙과 타입 기반 매칭 규칙은 별도의 명세에서 정의합니다.

- **문법 규격:**
    - `[num]` : 숫자 패턴 지정 (`\d` 대응)
    - `[str]` : 문자 패턴 지정 (`\w` 대응)
    - `+` / `*` : 1개 이상 연속 매칭 / 0개 이상 매칭 기호 연동
    - `최소~최대` : 지정 글자 수 자릿수 범위 매칭 바인딩
    - `[match:abc]` : 제시된 단어 중 단 하나와 대응
- **예시:**

```cuff
note: 휴대폰 번호 검사 (010-숫자4개-숫자4개 패턴 검증)
if phone is "010-[num]4-[num]4" do: print("올바른 번호") end

note: 이메일 형식 검사 (대소문자 무시 검증)
if email IS "[str]+@[str]2~10.com" do: print("통과") end
```

---

### 7\. 컬렉션(List, Map) 데이터 조작 문법 (add, remove, replace to)

- **설명:** 연속형 데이터 묶음인 리스트(list)와 키-값 쌍의 맵(map)을 다룰 때, 마침표 메서드(`.append()`) 대신 자연어 구문 구조로 데이터 입출력을 통제합니다.

    리스트에 새로운 단일 원소를 맨 뒤로 추가 적재할 때는 `add to` 명령어로 독립된 줄을 개설하며, 리스트 내부의 특정 인덱스 칸 값을 수정하거나 맵 구조 내에 특정 키 값을 새로 추가 및 변경할 때는 **`replace to`** 구조를 사용합니다. 원소를 제거할 때는 `remove from`을 사용합니다.

    리스트의 대괄호(`[]`) 내부에는 인덱스를 사용하며, 인덱스는 정수 값이어야 합니다. 맵의 대괄호(`[]`) 내부에는 키를 사용합니다. 맵의 키는 문자열 리터럴 또는 적절한 값으로 평가되는 변수를 사용할 수 있으며, 자료구조의 성격에 맞지 않는 값은 허용하지 않습니다.

- **문법:**
    - 리스트 맨 뒤 원소 삽입: `add [추가할값] to [리스트명]`
    - 리스트 특정 인덱스 수정: `replace [리스트명][인덱스] to [새로운값]`
    - 맵 특정 키 추가 및 수정: `replace [맵이름]["키값"] to [새로운값]`
    - 컬렉션 원소 파괴 및 이탈: `remove [인덱스 혹은 키 혹은 실제값] from [컬렉션명]`
- **예시:**

```cuff
set list inventory to ["sword", "shield"]

add "potion" to inventory
replace inventory[1] to "magic_staff"
remove 2 from inventory

set map user_profile to {"name": "Bob"}

replace user_profile["level"] to 50
remove "level" from user_profile
```

---

### 8\. 조건문 및 3대 즉시 실행 반복문 (if, loop, end, stop)

- **설명:** 조건 분기 처리는 `if`, `else if`, `else`체인을 사용하며 실행부 코드 영역으로 전환되기 직전 마디에 `do:` 키워드를 배치합니다.

    반복 처리하는 루프(loop)문은 메모리 변수 상주 작업이 아닌 즉시 명령 실행의 기조를 띠므로 문두에 변수 생성자 `set`을 절대로 붙이지 않습니다.

    지정 범위 영역만큼 수동 순회하는 `loop repeat`, 조건식이 참인 동안 실행하는 `loop while`, 그리고 **조건문 안에 repeat 반복문을 넣은 것과 같은 효과를 내는 `loop match`** 3종으로 설계되었습니다.

    제어문 블록을 마감하는 종착역은 `end` 키워드가 마크하며, 짧은 구조의 실행부는 개발자의 시각적 선택에 맞춰 `end`라인을 한 줄로 연이어 밀착 결합하는 축약 포맷을 허용합니다.

    루프, 함수, 조건문 등 블록형 구문은 Python과 같이 들여쓰기를 필수로 요구합니다. 단, 한 줄 안에서 모든 실행부와 종료부가 완결되는 축약형 실행문은 들여쓰기 규제의 대상에서 제외합니다. 즉, 한 줄 축약형은 별도의 완결된 구문으로 취급하며 들여쓰기를 요구하지 않습니다. 여러 줄에 걸쳐 실행부를 작성하는 블록형 구문은 반드시 들여쓰기를 사용해야 하며, 들여쓰기가 없는 블록형 코드는 허용하지 않습니다. 빈 줄은 허용합니다.

    `end`는 생략하거나 부족하게 작성하거나 불필요하게 추가할 수 없습니다. 중첩된 블록을 포함하여 열린 블록의 수와 닫는 `end`의 수가 정확히 일치해야 하며, 파서는 잘못된 `end` 개수를 임의로 허용하지 않습니다.

    루프의 즉시 탈출은 `stop` 키워드를 사용하며, `stop`은 **가장 가까운 loop 하나만 종료**합니다.

- **문법:**
    - 다중 조건문 라인:
      `if [조건] do: [코드] else if [조건] do: [코드] else do: [코드] end`
    - 범위 반복 제어:
      `loop repeat [루프변수] to [시작값] ~ [끝값] do: [코드] end`
    - 논리 조건 반복:
      `loop while [논리조건식] do: [코드] end`
    - 조건-반복 결합 제어:
      `loop match [식별대상] is/IS [타겟상태] do: [코드] end`
- **예시:**

```cuff
if score >= 90 do: print("우수") else if score >= 80 do: print("장려") else do: print("노력") end

loop repeat i to 1 ~ 10 do:
    if i is 4 do:
        stop
    end
    print(f"회전 라운드: {i}")
end
```

---

### 9\. 함수의 고급 정의 및 제어 (async, returnable, await)

- **설명:** 함수의 정의 영역은 가독성 보존을 위해 **한 줄 뭉치기 작성을 문법적으로 절대 금지**하며, 반드시 물리적인 엔터(줄바꿈) 처리를 이행한 뒤 `end` 독립 행으로 수렴시켜야 합니다.

    변수 선언 일관성에 따라 문두는 항상 `set`으로 돌파하며, 파라미터는 Python처럼 자유로운 동적 타입 스타일(타입 생략 가능)을 채택해 가벼움을 확보합니다.

    비동기는 `async`, 반환값 존재는 `returnable`을 배치하여 선언하며 값을 실제로 내보낼 때는 `return`을 사용합니다.

    `async` 비동기 함수를 구동시켜 완료될 때까지 대기시킬 때는 호출문 정면에 `await`을 배치합니다.

    호출 시점부에는 헷갈림을 유발하는 `do:` 기호를 단절하고 오직 괄호`()`만 사용합니다.

    함수 정의는 반드시 여러 줄의 블록형 구문으로 작성하며, 함수 본문은 반드시 들여쓰기를 사용해야 합니다. 함수 정의에는 한 줄 축약형을 허용하지 않습니다. 따라서 들여쓰기가 없는 함수 본문은 허용하지 않습니다. 빈 줄은 허용합니다. 함수의 `end` 역시 반드시 독립적으로 작성해야 하며 생략할 수 없습니다.

    비동기 함수의 실제 실행 모델과 세부 동작은 별도의 구현 명세에서 정의합니다.

- **문법:**
    - 순수 보이드 함수 정의:
      `set function [함수명]([매개변수]) do: [줄바꿈] [실행코드] end`
    - 결괏값 리턴 함수 정의:
      `set returnable function [함수명](...) do: [줄바꿈] return [출력값] end`
    - 비동기 함수 정의:
      `set async function [함수명](...) do: [줄바꿈] [실행코드] end`
    - 실행 통제 자율 인계:
      `set [자료형] [받을변수] to await [비동기함수명]()`
- **예시:**

```cuff
set returnable function calculate_bonus(base_pay) do:
    set constant number MULTIPLIER to 2
    return base_pay * MULTIPLIER
end

set async function download_graphics() do:
    print("그래픽 데이터를 비동기로 로드합니다.")
end

set number final_reward to calculate_bonus(5000)
await download_graphics()
```

---

### 10\. 안전장치 에러 핸들링 문법 (or_else do:)

- **설명:** 무겁고 가독성을 해치는 기존 언어의 `try-catch` 블록 대신, 에러가 발생할 위험이 있는 함수나 명령 행 바로 뒤에 한 칸 띄우고 `or_else do:`구문을 결합하여 예외 상황을 즉시 처리합니다.

    `or_else`의 구체적인 실행 방식 및 반환값 처리 규칙은 별도의 구현 명세에서 정의합니다.

- **문법:** `[위험한구문] or_else do: [에러시실행코드] end`

- **예시:**

```cuff
set str config to read_file("config.txt") or_else do:
    print("파일 읽기 실패! 기본 환경 옵션을 대신 불러옵니다.")
    set str config to "default_mode"
end
```

---

### 11\. 화면 출력 및 키보드 입력 기본 함수 (print, input)

- **설명:** 화면에 텍스트를 출력하는 기능은 대중적인 `print()`를 사용하고, 키보드로부터 사용자의 텍스트 입력을 직접 전달받는 기능은 표준적인 `input()` 함수를 사용하여 범용성과 직관성을 지킵니다.

    변수 내부 삽입은 문자열 정면에 접두사 `f`를 붙이는 f-스트링 방식을 따릅니다.

- **문법:**
    - `print([값])`
    - `input([안내메시지])`
- **예시:**

```cuff
set str user_name to input("이름을 입력해 주세요: ")
print(f"환영합니다, {user_name}님!")
```

---

### 12\. 모듈 및 라이브러리(DLC) 로드 시스템 (use & from)

- **설명:** CuffScript 공식 내장 라이브러리 패키지 세트는 본 언어만의 유머 코드를 적극 투영하여 DLC라고 명칭을 호출합니다.

    개발자가 메인 소스코드 파일 외부에 직접 생성해 놓은 로컬 파일 컴포넌트를 결합하고자 할 때는 `use` 키워드와 파일 상대경로를 찍어주는 `from` 연산 체계를 가동합니다.

    패키지 호출부의 파편화를 완전히 진압하기 위해, 모듈 로드와 관련된 모든 명령 구문은 무조건 가로 단 한 줄(Single Line)로만 코드를 완결 지어야 하는 문법 규격을 강제 부여합니다.

- **문법:**
    - 내장 공식 라이브러리 흡수: `use DLC:[코어라이브러리명]`
    - 커스텀 로컬 모듈 부품 흡수: `use [모듈파일명] from [상대폴더경로]`
- **예시:**

```cuff
use DLC:network
use dlc_graphic_pack from ./assets/plugins
```

---

# CuffScript 종합 검증 코드

```cuff
note: 1단계: 외부 공식 라이브러리(DLC) 및 커스텀 모듈 로드 (한 줄 작성 규칙 엄수)
use DLC:network
use stage_data from ./maps/core_engine

note:
이 영역은 여러 줄 주석 영역입니다.
기존 대화의 1-Based 인덱스, or_else, 대입 규칙 등이 싹 통합된 최종 검증용 코드입니다.
endnote

note: 2단계: 핵심 변수 및 고정 상수 라인 마운트 (상수는 오직 to만 사용)
set constant number MAX_LEVEL to 99
set constant str ENGINE_SIGNATURE to "CUFF_LANG_V1"

set number current_lvl to 1
set str user_email to "Player_One@CuffLang.com"
set list reward_tier_list to ["Gold", "Silver", "Bronze"]

note: 3단계: 논리 검증 및 텍스트 정보 파싱을 담당하는 지능형 리턴 제어 함수 선언 (한 줄 배치 절대 엄금)
set returnable function audit_and_assess_user(email, lvl) do:

    note: JS 스펙의 파워를 계승한 초간결 정규식 및 대문자 IS 판정 연산 가동
    if email IS "[str]+@[str]2~10.com" do:
        print("정규식 패턴 매칭 엔진: 이메일 구조 정밀 도메인 식별 및 통과 완료")
    end

    note: 느낌표(!) 표준 부정 연산자와 소문자 is 엄격 동등 매칭 연산의 시너지
    if !lvl is MAX_LEVEL do:
        print(f"현재 레벨 {lvl}은 최고 레벨 상태가 아닙니다.")
    end

    if lvl is MAX_LEVEL do:
        return "epic_rank"
    else if lvl >= 50 do:
        return "rare_rank"
    else do:
        return "common_rank"
    end
end

note: 4단계: 비동기 데이터 처리를 대행하는 독립형 스레드 함수 개설
set async function backup_user_cloud_data() do:
    print("가상 머신 내부 데이터 스냅샷을 원격 클라우드 인프라로 전송 동기화합니다.")
end

note: 5단계: 메인 런타임 비즈니스 로직 실행 및 1-Based 컬렉션 데이터 조작 테스팅
set str evaluation_result to audit_and_assess_user(user_email, current_lvl)

note: or_else 안전장치 탑재형 함수 실행 구문 테스트
await backup_user_cloud_data() or_else do:
    print("네트워크 환경으로 인한 클라우드 동기화 실패 상황을 우회합니다.")
end

note: 1-Based 인덱싱에 의거해 1번이 즉시 첫 번째 알맹이인 "Gold"를 겨냥합니다.
print(f"최고 등급의 보상 엠블럼 식별 데이터: {reward_tier_list[1]}")

note: 독립 명령어 구문인 add to 와 replace to를 통해 리스트 내부 정보 수정 가공
add "None_Tier" to reward_tier_list
replace reward_tier_list[1] to "Platinum_Tier" note: 1번째 주소의 기존 단어를 플래티넘으로 변경

note: 6단계: 물결 범위 지시 기호(~)와 loop repeat 제어 구조를 활용한 초고속 즉시 실행 반복 제어
loop repeat step to 1 ~ 5 do:
    if step is 4 do:
        print("반복 강제 정지를 발동합니다.")
        stop
    end
    print(f"CuffScript 고속 가상 머신 동기화 엔진 가동 중... 현재 루프 마디 번호: {step}")
end
```
