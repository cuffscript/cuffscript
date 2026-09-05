# CuffScript 정규식 명세서

> CuffScript 정규 표현식 및 패턴 매칭 명세서
> Version: 2.0

---

## 1. 목적

CuffScript의 정규 표현식(Regex)은 JavaScript 수준의 강력한 텍스트 검증 및 가공 능력을 유지하면서, 복잡하고 난해한 특수기호(`\d`, `\w`, `\s`, `(?<=...)`, `\1` 등)를 배제하고 직관적인 단어형 토큰(`[num]`, `[str]` 등)과 자연어 스타일 구문으로 재설계되었습니다.

프로그래밍 입문자가 일상적인 업무 자동화, 웹 데이터 수집, 데이터 정제 스크립트를 작성할 때 패턴을 직관적으로 이해하고 즉시 적용할 수 있도록 지원하는 것을 목적으로 합니다.

---

## 2. 기본 사용법과 조건문 결합 (`is`, `IS`)

CuffScript에서 패턴 검증은 별도의 함수 호출 없이 비교 연산자 `is` 또는 `IS`를 통해 즉시 수행할 수 있습니다.

```cuff
set str user_code to "AB-1234"

if user_code is "[up]2-[num]4" do:
    print("규격에 맞는 코드입니다.")
end
```

문자열 리터럴 안에 대괄호 토큰(`[num]`, `[str]` 등)이나 수량자 기호가 포함되어 있으면 엔진은 이를 정규식 패턴으로 자동 인식합니다.

---

## 3. 대소문자 판정 엔진 (`is` vs `IS`)

- **`is`**: 영문 알파벳의 대소문자를 엄격하게 구분하여 비교합니다.
- **`IS`**: 영문 알파벳의 대소문자 구분을 완전히 소멸시키고 대소문자 무시(Case-Insensitive) 패턴 매칭을 수행합니다.

```cuff
set str answer to "Yes"

note: 소문자 yes만을 기대하므로 거짓(false) 판정
if answer is "yes" do:
    print("소문자 일치")
end

note: IS 연산자를 사용하여 대소문자 무시 참(true) 판정
if answer IS "yes" do:
    print("대소문자 상관없이 통과")
end
```

---

## 4. 전체 일치(Full Match) 기본 원칙

CuffScript의 조건문 검증은 기본적으로 대상 문자열의 **처음부터 끝까지 전체가 일치**하는지 검사합니다.

따라서 타 언어 정규식처럼 시작 앵커(`^`)나 끝 앵커(`$`)를 붙일 필요가 없습니다.

```cuff
set str pin to "1234"

note: 정확히 4자리 전체가 일치해야 참이 됩니다.
if pin is "[num]4" do:
    print("올바른 PIN 번호")
end
```

- `"1234"` → 참 (`true`)
- `"123"` → 거짓 (`false`)
- `"A123"` → 거짓 (`false`)
- `"12345"` → 거짓 (`false`)

---

## 5. 기본 문자 단위 토큰

외우기 어려운 이스케이프 기호 대신, 대괄호로 감싼 직관적인 단어형 토큰을 제공합니다.

| 패턴 토큰 | 의미                    | 매칭 범위                                |
| :-------- | :---------------------- | :--------------------------------------- |
| `[num]`   | 숫자 1개                | `0` ~ `9`                                |
| `[let]`   | 영문 알파벳 1개         | `a` ~ `z`, `A` ~ `Z`                     |
| `[low]`   | 영문 소문자 1개         | `a` ~ `z`                                |
| `[up]`    | 영문 대문자 1개         | `A` ~ `Z`                                |
| `[sp]`    | 공백 문자 1개           | 스페이스(공백), 탭(`\t`)                 |
| `[nl]`    | 줄바꿈 문자 1개         | 개행 문자(`\n`, `\r\n`)                  |
| `[any]`   | 임의의 문자 1개         | 줄바꿈을 제외한 세상의 모든 글자 및 기호 |

---

## 6. `[str]` 토큰 (영문자 및 숫자)

`[str]`은 프로그래밍에서 가장 흔히 검사하는 **영문 알파벳과 숫자의 조합** 1글자를 나타냅니다.

- 매칭 대상: `A-Z`, `a-z`, `0-9`
- 특수문자(`_`, `-`, `@`, `.` 등)는 포함되지 않습니다.

```cuff
set str id to "user2026"

if id is "[str]8" do:
    print("영문/숫자 8글자 일치")
end
```

---

## 7. `[word]` 토큰 (식별자 및 단어 문자)

`[word]`는 영문자, 숫자뿐만 아니라 **언더바(`_`) 기호**까지 포함하는 단어 구성 문자 1개를 의미합니다.

- 매칭 대상: `A-Z`, `a-z`, `0-9`, `_`
- 변수명, 계정 ID 등에 특수문자 `_`를 허용할 때 주로 사용합니다.

```cuff
set str account to "admin_cuff_01"

if account is "[word]+" do:
    print("유효한 계정 식별자 형식입니다.")
end
```

---

## 8. 정확한 반복 횟수 (`N`)

토큰 뒤에 양의 정수 숫자를 붙이면 **해당 횟수만큼 정확히 반복**됨을 의미합니다. 중괄호(`{N}`) 기호를 사용할 필요가 없습니다.

```cuff
note: 숫자 6자리 (생년월일 형식)
"[num]6"

note: 영문 대문자 3자리
"[up]3"

note: 영문자/숫자 4자리
"[str]4"
```

---

## 9. 최소 반복 수량자 (`+`, `*`)

- `+` : 직전 원자가 **1개 이상** 존재해야 함 (최소 1번)
- `*` : 직전 원자가 **0개 이상** 존재해야 함 (없어도 되고 여러 개 있어도 됨)

```cuff
note: 공백이 1개 이상 연속됨
"[sp]+"

note: 숫자가 아예 없거나 여러 개 연속됨
"[num]*"
```

---

## 10. 선택적 존재 수량자 (`?`)

`?`는 직전 원자가 **0개 또는 1개** 존재함을 나타냅니다. (있어도 되고 없어도 되는 조건)

```cuff
note: http 또는 https 둘 다 허용
if protocol is "https?" do:
    print("웹 프로토콜 확인")
end

note: color(미국식) 및 colour(영국식) 둘 다 일치
if text is "colou?r" do:
    print("단어 일치")
end
```

---

## 11. 물결 범위 반복 수량자 (`~`)

CuffScript의 고유 문법인 물결(`~`) 기호를 사용하여 최소 횟수와 최대 횟수를 직관적으로 지정합니다.

- `N~M` : 최소 N개 이상, 최대 M개 이하 (Inclusive)
- `N~` : 최소 N개 이상 (상한선 없음)
- `~M` : 최대 M개 이하 (최소 0개부터 M개까지)

```cuff
note: 비밀번호 영문/숫자 8자리 이상 16자리 이하
if password is "[str]8~16" do:
    print("적절한 길이의 비밀번호")
end

note: 영문자 2글자 이상 무제한
if code is "[let]2~" do:
    print("통과")
end
```

---

## 12. 탐욕(Greedy) 및 게으른(Lazy) 매칭

기본 수량자(`+`, `*`, `~`)는 가능한 한 가장 긴 문자열을 삼키는 **Greedy(탐욕적)** 방식으로 동작합니다.

수량자 바로 뒤에 `?`를 덧붙이면 가능한 한 가장 짧게 일치하는 **Lazy(소극적)** 방식으로 전환됩니다.

```cuff
note: Greedy 방식: 첫 번째 <tag>부터 맨 마지막 </tag>까지 전부 삼킴
"<tag>[any]*</tag>"

note: Lazy 방식: 가장 가까운 </tag>를 만나면 즉시 매칭 완료
"<tag>[any]*?</tag>"
```

---

## 13. 소괄호 그룹 (`(...)`)

여러 패턴 토큰을 하나로 묶어 수량자를 적용하거나 연산 단위를 만들 때는 소괄호 `()`를 사용합니다.

```cuff
note: "AB"라는 단어 세트가 3번 반복 ("ABABAB")
if code is "(AB)3" do:
    print("반복 코드 통과")
end

note: "숫자2개-문자2개" 묶음이 1개 이상 연결
if token is "([num]2-[let]2)+" do:
    print("복합 토큰 일치")
end
```

---

## 14. 단어 선택 토큰 (`[one:...]`)

제시된 여러 후보 단어나 기호 중 **단 하나**와 일치해야 할 때는 `[one:후보1|후보2|...]` 구조를 사용합니다.

콜론 앞 공백 금지 규격을 준수해야 합니다.

```cuff
note: 파일 확장자 검사
if ext is "[one:jpg|png|gif|webp]" do:
    print("지원하는 이미지 포맷입니다.")
end

note: 결제 수단 판정
if payment is "[one:card|cash|point]" do:
    print("정상 결제 수단")
end
```

---

## 15. 사용자 정의 문자 세트 (`[...]`)

특정 문자 범위나 원하는 글자들의 집합을 직접 지정할 때는 대괄호 안에 문자 목록을 나열합니다.

- `[abc]` : `a`, `b`, `c` 중 문자 1개
- `[a-z]` : 영문 소문자 범위 중 1개
- `[0-9]` : 숫자 범위 중 1개 (`[num]`과 동일)
- `[A-Z]` : 영문 대문자 범위 중 1개

```cuff
note: 주민등록번호 성별 식별 숫자 (1, 2, 3, 4 중 하나)
if gender_code is "[1234]" do:
    print("올바른 성별 식별 번호")
end
```

---

## 16. 부정 문자 세트 (`[!...]`)

대괄호 문자 세트의 첫 글자로 느낄표(`!`)를 배치하면, **해당 문자들을 제외한 나머지 문자**와 매칭됩니다.

- `[!0-9]` : 숫자가 아닌 모든 문자 1개
- `[!a-z]` : 영문 소문자가 아닌 모든 문자 1개
- `[!sp]` : 공백이 아닌 모든 문자 1개

```cuff
note: 공백이 전혀 없는 문자열이 1개 이상 지속
if input_data is "[!sp]+" do:
    print("공백이 포함되지 않은 유효한 단어")
end
```

---

## 17. 단어 경계 토큰 (`[edge]`)

JavaScript의 `\b`에 대응하며, 단어와 비단어(공백, 구두점, 문장의 시작과 끝) 사이의 **경계 지점**을 의미합니다.

단어 경계를 사용하면 더 긴 단어 속에 포함된 부분 문자열 오탐지를 차단합니다.

```cuff
set str sentence to "The category of catalog is books"

note: 'cat'이라는 독립된 단어만 검색 (category나 catalog는 제외)
set list result to find "[edge]cat[edge]" from sentence g

print(result) note: 빈 결과 반환 (cat 단독 단어가 없음)
```

---

## 18. 수치 데이터 프리셋 토큰 (`[int]`, `[float]`, `[hex]`)

자주 사용하는 숫자 형식을 매번 정규식 기호로 조립하지 않고 즉시 사용할 수 있는 고급 단축 토큰을 제공합니다.

| 프리셋 토큰 | 설명                                  | 매칭 예시               |
| :---------- | :------------------------------------ | :---------------------- |
| `[int]`     | 부호를 포함할 수 있는 정수            | `100`, `-25`, `+7`      |
| `[float]`   | 부호를 포함할 수 있는 소수점 실수     | `3.14`, `-0.05`, `10.0` |
| `[hex]`     | 16진수 문자 1개 (`0-9`, `a-f`, `A-F`) | `A`, `f`, `9`           |

```cuff
set str coordinate to "-12.54"

if coordinate is "[float]" do:
    print("유효한 좌표 실수값입니다.")
end
```

---

## 19. 실무 포맷 프리셋 토큰 (`[email]`, `[phone]`, `[url]`)

입문자들이 가장 많이 작성하는 정형 데이터 검증을 단축 토큰 하나로 완결할 수 있습니다.

| 프리셋 토큰 | 설명                          | 매칭 규격                           |
| :---------- | :---------------------------- | :---------------------------------- |
| `[email]`   | 표준 인터넷 전자우편 주소     | `계정@도메인.최상위도메인`          |
| `[phone]`   | 대한민국 유무선 전화번호 규격 | `010-XXXX-XXXX`, `02-XXX-XXXX` 등   |
| `[url]`     | 웹 주소 프로토콜 규격         | `http://` 또는 `https://` 시작 주소 |

```cuff
set str user_contact to "010-8888-9999"

if user_contact is "[phone]" do:
    print("올바른 대한민국 전화번호 형식입니다.")
end
```

---

## 20. 특수 기호 이스케이프 (`\`)

패턴 내부에서 문법적 의미를 지니는 특수 문자들을 순수한 글자 그 자체로 취급하려면 역슬래시(`\`)를 붙여야 합니다.

- 이스케이프 대상 기호: `[`, `]`, `(`, `)`, `<`, `>`, `+`, `*`, `?`, `~`, `|`, `.`, `\`, `:`

```cuff
note: 실제 점(.) 기호를 검사할 때는 이스케이프가 필요합니다.
if filename is "[str]+\.png" do:
    print("PNG 파일 확장자입니다.")
end

note: 괄호 기호 자체를 검사할 때
if math_exp is "\([num]+\)" do:
    print("괄호로 감싸진 숫자입니다.")
end
```

---

## 21. 콜론 공백 엄격 준수 규칙

CuffScript의 언어적 대원칙에 따라, 정규식 내부 토큰(`[one:...]`, `<name:...>`)에서도 **콜론 앞 공백은 절대 금지**됩니다.

이를 위반하면 렉서 단계에서 즉시 정규식 문법 에러(`Regex Syntax Error`)를 발생시킵니다.

- **올바른 예:** `"[one:apple|banana]"`, `"<price:[num]+>"`
- **잘못된 예:** `"[one :apple|banana]"`, `"<price :[num]+>"`

---

## 22. 1-Based 인덱스 캡처 추출 (`match`)

문자열에서 소괄호 `()`로 감싼 부분의 실제 값을 추출할 때는 `match` 구문을 사용합니다.

언어 철학에 따라 결과 접근 인덱스는 **1번부터 시작**합니다.

매칭에 실패하면 결과 변수에는 `empty`가 대입됩니다.

```cuff
set str serial to "SN-2026-998"
set match result to match serial from "SN-([num]4)-([num]+)"

if result is not empty do:
    print(f"제작연도: {result[1]}") note: "2026"
    print(f"고유번호: {result[2]}") note: "998"
end
```

---

## 23. 이름 지정 캡처 (Named Capture: `<name:...>`)

소괄호 번호 대신 직관적인 이름을 지정하여 데이터를 추출할 수 있습니다. 추출된 결과는 맵(Map) 스타일로 키를 통해 조회합니다.

```cuff
set str date_text to "2026-12-25"
set match res to match date_text from "<year:[num]4>-<month:[num]2>-<day:[num]2>"

if res is not empty do:
    print(f"연도: {res['year']}")  note: "2026"
    print(f"월: {res['month']}")   note: "12"
    print(f"일: {res['day']}")     note: "25"
end
```

---

## 24. 본문 검색 및 전체 수집 (`find` & `g`)

전체 일치가 아닌, 긴 본문 속에서 패턴에 부합하는 조각을 찾아낼 때는 `find` 명령어를 사용합니다.

- 단독 사용: 최초로 발견된 **단 하나의 텍스트를 문자열로 반환**합니다.
- `g` 플래그 결합: 발견된 **모든 매칭 텍스트를 1-Based 리스트로 반환**합니다.
- 매칭 없을 시: **`empty` 반환**

```cuff
set str article to "티켓 번호: T-101, 다음 티켓: T-205, 보조 티켓: T-309"

note: 첫 매칭만 추출 (문자열 반환)
set str first_ticket to find "T-[num]3" from article
print(first_ticket) note: "T-101"

note: 모든 매칭 수집 (g 플래그, 1-Based 리스트 반환)
set list tickets to find "T-[num]3" from article g

print(tickets[1]) note: "T-101"
print(tickets[2]) note: "T-205"
print(tickets[3]) note: "T-309"

note: 매칭 없을 시 empty 반환
set str result to find "T-[num]5" from article
if result is empty do:
    print("매칭되는 패턴이 없습니다.")
end
```

---

## 25. 패턴 기반 문자열 치환 (`replace pattern in text to`)

정규식 패턴에 해당하는 구간을 다른 문자열로 갈아 끼울 때는 `replace in to` 자연어 구문을 사용합니다.

```cuff
set str raw_log to "전화번호: 010-1234-5678 개인정보"

note: 중간 번호 4자리를 마스킹(****) 처리
set str masked_log to replace "010-[num]4-" in raw_log to "010-****-"

print(masked_log) note: "전화번호: 010-****-5678 개인정보"
```

전체 치환을 수행하려면 구문 끝에 `g` 플래그를 붙입니다.

```cuff
set str clean_text to replace "[sp]+" in "Hello   Cuff    World" to " " g
print(clean_text) note: "Hello Cuff World"
```

---

## 26. 패턴 기반 문자열 분할 (`split text by`)

특정 기호나 패턴을 기준선 삼아 문자열을 여러 조각으로 쪼갈 때는 `split by` 구문을 사용합니다.

```cuff
set str tag_data to "사과, 배; 포도: 감귤"

note: 쉼표, 세미콜론, 콜론 뒤에 공백이 붙은 복합 구분자 기준으로 분할
set list fruit_list to split tag_data by "[,;:][sp]*"

loop repeat i to 1 ~ 4 do:
    print(f"과일 {i}: {fruit_list[i]}")
end
```

---

## 27. 패턴 출현 빈도 카운팅 (`count pattern in text`)

본문 텍스트 내에서 특정 패턴이 몇 번 등장하는지 즉시 정수 숫자로 계산할 수 있습니다.

매칭이 없을 시: **`0` 반환**

```cuff
set str document to "apple, banana, Apple, orange, APPLE"

note: 대소문자 무시(i) 상태로 등장 횟수 산출
set number apple_count to count "apple" in document i

print(f"사과 단어 등장 횟수: {apple_count}") note: 3

note: 매칭이 없으면 0 반환
set number grape_count to count "grape" in document
print(f"포도 단어 등장 횟수: {grape_count}") note: 0
```

---

## 28. 플래그(Flags) 시스템

검색 구문(`find`, `match`, `count`, `replace`)의 맨 뒤에는 검색 방식을 튜닝하는 플래그를 덧붙일 수 있습니다.

| 플래그 | 명칭        | 기능 설명                                                |
| :----- | :---------- | :------------------------------------------------------- |
| `i`    | Ignore Case | 영문 대소문자를 구분하지 않고 검색합니다.                |
| `g`    | Global      | 첫 일치에서 멈추지 않고 텍스트 전체를 순회합니다.        |
| `m`    | Multiline   | 줄바꿈 문자를 기준으로 각 행마다 시작과 끝을 적용합니다. |

여러 플래그는 띄어쓰기 없이 연달아 작성할 수 있습니다: `gim`

```cuff
set list matches to find "error:[num]+" from server_logs gi
```

---

## 29. 부분 매칭용 앵커 토큰 (`[start]`, `[end]`)

`find`나 `replace` 등 텍스트 내부 부분 검색 구문을 사용할 때, 문장의 맨 앞이나 맨 뒤 위치를 강제하려면 앵커 토큰을 사용합니다. (전통 정규식의 `^`, `$`에 대응)

- `[start]` : 텍스트의 맨 처음 위치
- `[end]` : 텍스트의 맨 끝 위치

```cuff
set str message to "NOTICE: 점검이 시작됩니다."

note: 맨 처음에 위치한 공지 사항 헤더만 검색
set match alert to find "[start]NOTICE:" from message
```

---

## 30. 빈 패턴 및 공백 검사

- `""` (빈 패턴)은 길이가 0인 빈 문자열과만 매칭됩니다.
- 공백이 누락되거나 문법이 비어 있는 `[one:]` 등의 형태는 컴파일 단계에서 차단됩니다.

```cuff
set str empty_box to ""

if empty_box is "" do:
    print("비어있는 텍스트 확인")
end
```

---

## 31. 안전장치 `or_else`와의 결합

정규식을 활용한 매칭이나 파싱 작업이 실패하여 `empty`가 발생하거나 런타임 오류가 예상될 때, CuffScript 고유의 `or_else do:` 블록을 즉시 결합할 수 있습니다.

```cuff
set match user_info to match input_str from "<name:[str]+>-(<id:[str]+>)" or_else do:
    print("포맷 해석 실패! 기본 정보로 대체합니다.")
    change user_info to {"name": "guest", "id": "0000"}
end
```

---

## 32. 정규식 문법 에러 (Regex Syntax Error)

패턴 문자열 내부 문법이 올바르지 않으면 조용히 `false`를 내놓지 않고 렉서 및 파서 단계에서 즉시 `Regex Syntax Error`를 일으켜 버그를 신속히 격리합니다.

**문법 에러 발생 사례:**

- 닫히지 않은 괄호: `"(abc"`
- 닫히지 않은 대괄호: `"[num"`
- 잘못된 콜론 공백: `"[one :a|b]"`
- 역순 수량 범위: `"[num]5~2"` (시작값이 끝값보다 큼)
- 연속된 무의미한 중복 수량자: `"[num]++"`

---

## 33. 실행 한도 및 안전장치 (Regex ReDoS 방어)

엔진은 초보자의 미숙한 수량자 조합(`([any]+)+` 등)으로 인한 무한 루프(Catastrophic Backtracking)를 방지하기 위해 최대 매칭 스텝 수(Step Limit)와 시간 제한(Timeout)을 기본 탑재합니다.

지정된 임계치를 초과하면 시스템 폭주를 막기 위해 `Regex Runtime Error`를 송출하고 엔진을 안전하게 비상 정지시킵니다.

---

## 34. 실전 예제 1 — 회원가입 폼 데이터 유효성 검사

```cuff
set str user_id to "cuff_master"
set str user_pw to "pass1234!"
set str user_name to "Alice"

note: 아이디: 영문/숫자/_ 조합의 6~12자리
if !(user_id is "[word]6~12") do:
    print("아이디 형식 오류")
end

note: 비밀번호: 특수문자 포함 최소 8자리
if user_pw is "[str!]{8,}" do:
    print("올바른 비밀번호 형식")
end
```

---

## 35. 실전 예제 2 — 한국형 데이터 검증 (전화번호 및 사업자등록번호)

```cuff
set str mobile to "010-7777-8888"
set str biz_no to "123-45-67890"

note: 휴대전화 번호 검증
if mobile is "010-[num]4-[num]4" do:
    print("유효한 휴대폰 번호")
end

note: 사업자등록번호 3자리-2자리-5자리 검증
if biz_no is "[num]3-[num]2-[num]5" do:
    print("유효한 사업자등록번호 구조")
end
```

---

## 36. 실전 예제 3 — 서버 로그 파일 데이터 파싱

```cuff
set str log_line to "2026-03-31 [ERROR] 192.168.0.1 - DB Connection Lost"

set match parsed to match log_line from "<date:[num]4-[num]2-[num]2> \[[one:INFO|WARN|ERROR]\] <ip:[num]1~3\.[num]1~3\.[num]1~3\.[num]1~3> - <msg:[any]+>"

if parsed is not empty do:
    print(f"발생 일자: {parsed['date']}")
    print(f"클라이언트 IP: {parsed['ip']}")
    print(f"에러 메시지: {parsed['msg']}")
end
```

---

## 37. 실전 예제 4 — 문서 내 개인정보 마스킹 (치환)

```cuff
set str doc to "문의자 전화번호는 010-1111-2222 입니다."

note: 휴대전화 번호 전체를 찾아 안심 번호 라벨로 일괄 치환
set str secured_doc to replace "010-[num]4-[num]4" in doc to "[전화번호 비공개]"

print(secured_doc) note: "문의자 전화번호는 [전화번호 비공개] 입니다."
```

---

## 38. 실전 예제 5 — 자연어 텍스트 분할 및 단어 추출

```cuff
set str raw_tags to "python, cuff; javascript / kotlin"

note: 쉼표, 세미콜론, 슬래시 및 주변 공백을 묶어서 분할 기준으로 적용
set list tags to split raw_tags by "[sp]*[,;/][sp]*"

loop repeat idx to 1 ~ 4 do:
    print(f"등록 태그 #{idx}: {tags[idx]}")
end
```

---

## 39. CuffScript Regex 토큰 퀵 레퍼런스

```text
[문자 토큰]
  [num]       : 숫자 (0-9)
  [let]       : 영문 알파벳 (a-z, A-Z)
  [low]       : 영문 소문자 (a-z)
  [up]        : 영문 대문자 (A-Z)
  [str]       : 영문자 + 숫자 (a-zA-Z0-9)
  [word]      : 영문자 + 숫자 + 언더바 (식별자)
  [sp]        : 공백 문자 (띄어쓰기, 탭)
  [nl]        : 줄바꿈 문자
  [any]       : 임의의 문자 1개

[프리셋 토큰]
  [int]       : 부호 포함 정수
  [float]     : 부호 포함 실수
  [hex]       : 16진수 문자
  [email]     : 이메일 표준 포맷
  [phone]     : 전화번호 표준 포맷
  [url]       : 웹 주소 포맷
  [edge]      : 단어 경계 (Word boundary)
  [start]     : 문자열 시작 앵커
  [end]       : 문자열 종료 앵커

[수량자 및 선택]
  N           : 정확히 N개
  +           : 1개 이상
  *           : 0개 이상
  ?           : 0개 또는 1개
  N~M         : N개 이상 M개 이하
  N~          : N개 이상
  ~M          : M개 이하
  ? (접미)    : Lazy 매칭 모드

[선택 및 세트]
  [one:a|b]   : 단어 또는 기호 중 하나 선택
  [abc]       : 문자 세트
  [!abc]      : 부정 문자 세트

[그룹 및 추출]
  (...)       : 캡처 그룹 (1-Based 인덱스 접근)
  <name:...>  : 이름 지정 캡처 (키 값 조회)

[명령어 API]
  is / IS     : 전체 문자열 일치 판정 (IS는 대소문자 무시)
  match       : 패턴 캡처 추출 (실패 시 empty)
  find        : 본문 부분 검색 (g 플래그 지원, 실패 시 empty)
  replace     : 패턴 기반 텍스트 치환
  split       : 패턴 기준 텍스트 분할
  count       : 패턴 출현 횟수 계산 (없을 시 0)
```
