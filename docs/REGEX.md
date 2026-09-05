# CuffScript Regex Specification

> CuffScript Regular Expression / Pattern Matching Specification  
> Version: 1.0

---

## 1. 목적

CuffScript의 Regex는 JavaScript RegExp 수준의 패턴 표현력을 목표로 한다.

단, 기존 정규식의 복잡한 문법을 그대로 노출하지 않고 CuffScript의 짧고 직관적인 타입 패턴을 우선 사용한다.

```cuff
"[num]4"
"[str]+"
"010-[num]4-[num]4"
"[one:yes|no]"
```

Cuff Regex는 **표현력은 높게, 기본 문법은 짧게** 설계한다.

---

# 2. 기본 사용법

문자열 패턴은 `is` 또는 `IS`와 결합하여 검사한다.

```cuff
if value is "[num]4" do:
    print("4자리 숫자")
end
```

`is`는 일반적인 대소문자 구분 매칭을 수행한다.

```cuff
if value is "Cuff" do:
    ...
end
```

`IS`는 영어 알파벳의 대소문자를 무시한다.

```cuff
if value IS "Cuff" do:
    ...
end
```

패턴이 포함된 문자열은 일반 문자열 비교가 아닌 Regex 패턴으로 해석된다.

---

# 3. 전체 문자열 매칭

Cuff Regex는 기본적으로 **전체 문자열 매칭(Full Match)** 이다.

따라서 시작과 끝을 나타내는 `^`, `$`가 필요하지 않다.

```cuff
if code is "[num]4" do:
    ...
end
```

위 패턴은 정확히 숫자 4개인 문자열만 허용한다.

```text
1234    -> true
123     -> false
A123    -> false
12345   -> false
```

부분 문자열 검색이 필요한 경우에는 `find` 기능을 사용한다.

```cuff
find text with "[num]+"
```

`find`의 상세 규칙은 19장에서 정의한다.

---

# 4. 일반 문자

특수한 의미가 없는 문자는 입력 문자열의 동일한 문자와 매칭된다.

```cuff
"hello"
```

다음과 매칭된다.

```text
hello
```

다음과는 매칭되지 않는다.

```text
Hello
HELLO
hello!
```

`IS`를 사용하면 영어 알파벳의 대소문자를 무시한다.

```cuff
"hello"
```

```cuff
value IS "hello"
```

---

# 5. 기본 타입 패턴

Cuff는 자주 사용하는 문자 종류를 짧은 타입 패턴으로 제공한다.

| 패턴    | 의미                            |
| ------- | ------------------------------- |
| `[num]` | ASCII 숫자 1개 (`0-9`)          |
| `[str]` | 영문자 또는 숫자 1개            |
| `[let]` | 영문자 1개                      |
| `[up]`  | 영문 대문자 1개                 |
| `[low]` | 영문 소문자 1개                 |
| `[sp]`  | 공백 1개                        |
| `[any]` | 줄바꿈을 제외한 임의의 문자 1개 |
| `[nl]`  | 줄바꿈 1개                      |

예:

```cuff
"[num]"
"[let]"
"[up]"
"[low]"
```

---

# 6. `[str]`의 범위

`[str]`은 다음 문자만 허용한다.

```text
A-Z
a-z
0-9
```

따라서 `_`, `-`, `.`, `@` 등은 포함하지 않는다.

```cuff
"[str]+"
```

다음은 성공한다.

```text
abc
ABC
abc123
123ABC
```

다음은 실패한다.

```text
hello_world
hello-world
hello.world
```

문자 집합을 더 세밀하게 지정하려면 10장의 문자 집합 문법을 사용한다.

---

# 7. 반복

타입 패턴 및 그룹 뒤에 반복 연산자를 사용할 수 있다.

## 7.1 정확한 반복

```cuff
"[num]4"
```

숫자 정확히 4개.

```cuff
"[str]10"
```

문자/숫자 정확히 10개.

---

## 7.2 1개 이상

`+`

```cuff
"[num]+"
```

숫자 1개 이상.

```cuff
"[str]+"
```

문자/숫자 1개 이상.

---

## 7.3 0개 이상

`*`

```cuff
"[num]*"
```

숫자 0개 이상.

빈 문자열도 허용된다.

---

## 7.4 0개 또는 1개

`?`

```cuff
"[num]?"
```

숫자 0개 또는 1개.

```cuff
"colou?r"
```

다음과 매칭된다.

```text
color
colour
```

---

## 7.5 범위 반복

`최소~최대`

```cuff
"[num]2~5"
```

숫자 2개 이상 5개 이하.

---

## 7.6 최소값만 지정

```cuff
"[num]2~"
```

숫자 2개 이상.

최대값은 제한하지 않는다.

---

## 7.7 반복 우선순위

반복 연산자는 바로 앞의 **하나의 원자(atom)** 에 적용된다.

```cuff
"[num]+"
"[let]3"
"(abc)+"
```

`+`, `*`, `?`, `N`, `N~M`은 서로 중복하여 사용할 수 없다.

잘못된 예:

```cuff
"[num]+4"
"[num]*2"
"[num]2+"
```

이 경우 `Regex Syntax Error`가 발생한다.

---

# 8. 탐욕 / 최소 반복

기본 반복은 Greedy 방식이다.

필요한 경우 `?`를 반복 연산자 뒤에 붙여 Lazy 방식으로 변경한다.

```cuff
"[any]*?"
"[any]+?"
"[num]2~5?"
```

예:

```text
*   = 가능한 많이
*?  = 가능한 적게
+   = 가능한 많이
+?  = 가능한 적게
```

Lazy 반복은 `?` 하나를 사용한다.

따라서:

```cuff
"[num]??"
```

은 `[num]?`를 Lazy 방식으로 사용한다.

---

# 9. 그룹

괄호 `()`는 패턴을 하나의 그룹으로 묶는다.

```cuff
"(abc)+"
```

`abc` 전체를 반복한다.

다음과 매칭된다.

```text
abc
abcabc
abcabcabc
```

그룹에는 반복 연산자를 사용할 수 있다.

```cuff
"(ab)3"
"(ab)+"
"(ab)*"
"(ab)?"
"(ab)2~5"
```

---

# 10. 선택

Cuff는 선택을 위해 짧은 `[one:...]` 문법을 제공한다.

```cuff
"[one:cat|dog|bird]"
```

다음 중 하나와 매칭된다.

```text
cat
dog
bird
```

`one` 내부의 `|`는 선택 구분자다.

---

## 10.1 그룹과 선택

복잡한 선택은 그룹과 함께 사용할 수 있다.

```cuff
"(cat|dog)+"
```

또는 간단한 선택이라면:

```cuff
"[one:cat|dog]+"
```

를 권장한다.

---

## 10.2 선택 우선순위

선택지는 왼쪽부터 시도한다.

```cuff
"[one:cat|caterpillar]"
```

엔진은 `cat`을 먼저 시도한다.

필요한 경우 더 구체적인 패턴을 먼저 배치해야 한다.

---

# 11. 문자 집합

개별 문자 중 하나를 선택하려면 `[]` 문자 집합을 사용한다.

```cuff
"[abc]"
```

`a`, `b`, `c` 중 하나와 매칭된다.

```cuff
"[a-z]"
```

`a`부터 `z` 중 하나와 매칭된다.

```cuff
"[0-9]"
```

숫자 하나와 매칭된다.

---

## 11.1 여러 범위

```cuff
"[a-zA-Z0-9]"
```

영문자 또는 숫자 하나.

---

## 11.2 부정 문자 집합

문자 집합의 시작에 `!`를 사용한다.

```cuff
"[!0-9]"
```

숫자가 아닌 문자 하나.

```cuff
"[!a-z]"
```

소문자가 아닌 문자 하나.

---

## 11.3 문자 집합과 반복

```cuff
"[a-z]+"
"[0-9]4"
"[a-f0-9]+"
```

---

# 12. 타입 패턴과 문자 집합의 차이

다음은 서로 다른 문법이다.

```cuff
"[num]"
"[0-9]"
```

기본적으로 동일한 의미를 가진다.

반면:

```cuff
"[str]"
"[a-zA-Z0-9]"
```

도 기본적으로 동일한 범위를 가진다.

타입 패턴은 **가독성을 위해 우선 사용**한다.

---

# 13. Escape

Regex에서 특별한 의미를 가진 문자를 그대로 매칭하려면 `\`를 사용한다.

```cuff
"\+"
"\*"
"\?"
"\["
"\]"
"\("
"\)"
"\{"
"\}"
"\."
"\|"
"\~"
"\<"
"\>"
"\:"
"\-"
"\\"
```

예:

```cuff
if value is "10\+20" do:
    ...
end
```

`10+20`과 매칭된다.

---

# 14. 기본 Escape 패턴

Cuff는 자주 사용하는 Escape 패턴을 지원한다.

| 패턴 | 의미        |
| ---- | ----------- | --- |
| `\n` | 줄바꿈      |
| `\t` | 탭          |
| `\r` | 캐리지 리턴 |
| `\\` | `\`         |
| `\[` | `[`         |
| `\]` | `]`         |
| `\(` | `(`         |
| `\)` | `)`         |
| `\+` | `+`         |
| `\*` | `*`         |
| `\?` | `?`         |
| `\|` | `           | `   |
| `\~` | `~`         |
| `\.` | `.`         |

---

# 15. 특수 문자

다음 문자는 특별한 의미를 가진다.

```text
[
]
(
)
{
}
+
*
?
~
|
\
<
>
```

특수한 의미가 필요하지 않을 경우 `\`로 Escape한다.

---

# 16. 반복 블록

그룹을 사용하면 복잡한 패턴을 반복할 수 있다.

```cuff
"(ab)3"
```

```text
ababab
```

```cuff
"(ab)+"
```

```text
ab
abab
ababab
...
```

---

# 17. 비캡처 그룹

캡처가 필요 없는 그룹은 `(?:...)`을 사용한다.

```cuff
"(?:cat|dog)+"
```

비캡처 그룹은 결과 캡처 번호를 생성하지 않는다.

일반 그룹:

```cuff
"(cat|dog)"
```

비캡처 그룹:

```cuff
"(?:cat|dog)"
```

---

# 18. 캡처

일반 그룹 `()`은 캡처 그룹으로 취급한다.

```cuff
"([num]3)-([num]4)"
```

입력:

```text
010-1234
```

캡처 결과:

```text
1 = 010
2 = 1234
```

캡처는 1-Based 인덱싱을 따른다.

---

# 19. Match

Regex의 결과 자체가 필요할 경우 `match` 명령을 사용한다.

```cuff
set match result to match value with "([num]3)-([num]4)"
```

성공하면 `result`에는 Match 객체가 저장된다.

```cuff
print(result[1])
print(result[2])
```

실패하면 `result`는 `empty`가 된다.

---

# 20. Find

문자열 전체가 아닌 내부에서 패턴을 찾으려면 `find`를 사용한다.

```cuff
set match result to find text with "[num]+"
```

예:

```text
"abc123xyz456"
```

결과:

```text
123
456
```

`find`는 모든 매칭 결과를 1-Based 순서로 반환한다.

```cuff
result[1]
result[2]
```

---

# 21. Named Capture

캡처에 이름을 붙일 수 있다.

문법:

```cuff
"<name:pattern>"
```

예:

```cuff
"<year:[num]4>-<month:[num]2>-<day:[num]2>"
```

입력:

```text
2026-09-05
```

결과:

```text
year  = 2026
month = 09
day   = 05
```

이름으로 접근한다.

```cuff
result["year"]
result["month"]
result["day"]
```

---

# 22. Named Capture와 일반 Capture

다음은 일반 캡처다.

```cuff
"([num]4)"
```

다음은 이름 있는 캡처다.

```cuff
"<year:[num]4>"
```

Named Capture도 일반 캡처 번호를 하나 소비한다.

따라서:

```cuff
"(<year:[num]4>)-([num]2)"
```

에서는:

```text
result[1] = year
result[2] = 두 번째 숫자 그룹
```

이 된다.

---

# 23. Backreference

이전에 캡처한 문자열을 다시 사용하려면 `\N`을 사용한다.

```cuff
"([let]+)-\1"
```

입력:

```text
abc-abc
```

성공.

```text
abc-def
```

실패.

`\1`은 첫 번째 캡처를 의미한다.

```text
\1
\2
\3
...
```

---

# 24. Named Backreference

Named Capture는 `\k<name>`으로 다시 참조한다.

```cuff
"<word:[let]+>-\k<word>"
```

입력:

```text
hello-hello
```

성공.

```text
hello-world
```

실패.

---

# 25. Lookahead

앞에 특정 패턴이 존재하는지만 검사하려면 `(?=...)`를 사용한다.

```cuff
"[num]+(?=원)"
```

숫자 뒤에 `원`이 존재해야 한다.

Lookahead 자체는 `원`을 소비하지 않는다.

---

# 26. Negative Lookahead

앞에 특정 패턴이 존재하지 않아야 하는 경우 `(?!...)`를 사용한다.

```cuff
"[num]+(?!원)"
```

숫자 뒤에 `원`이 존재하지 않아야 한다.

---

# 27. Lookbehind

뒤에 특정 패턴이 존재하는지 검사하려면 `(?<=...)`를 사용한다.

```cuff
"(?<=USD)[num]+"
```

다음과 같은 문자열의 숫자 부분을 찾을 수 있다.

```text
USD100
```

Lookbehind 자체는 문자를 소비하지 않는다.

---

# 28. Negative Lookbehind

뒤에 특정 패턴이 존재하지 않아야 하는 경우 `(?<!...)`를 사용한다.

```cuff
"(?<!USD)[num]+"
```

`USD` 바로 뒤에 있는 숫자는 매칭하지 않는다.

---

# 29. Dot

`.`은 줄바꿈을 제외한 임의의 문자 하나를 의미한다.

```cuff
"."
```

Cuff의 기본 `[any]`와 동일한 의미를 가진다.

따라서 다음 두 패턴은 동일하다.

```cuff
"."
"[any]"
```

가독성을 위해 `[any]` 사용을 권장한다.

---

# 30. Dotall

줄바꿈까지 `.`에 포함하려면 `s` 플래그를 사용한다.

```cuff
match text with "." s
```

또는:

```cuff
match text with ".*" s
```

---

# 31. Word Boundary

단어 경계를 검사하려면 `\b`를 사용한다.

```cuff
"\bcat\b"
```

`cat`이라는 독립 단어와 매칭된다.

다음은 매칭된다.

```text
cat
a cat
cat!
```

다음은 매칭되지 않는다.

```text
catalog
bobcat
```

---

# 32. Non-Word Boundary

단어 경계가 아닌 위치를 검사하려면 `\B`를 사용한다.

```cuff
"\Bcat\B"
```

---

# 33. Digit / Word / Space Shortcut

JavaScript 정규식 호환성을 위해 다음 Escape를 지원한다.

| 패턴 | 의미             |
| ---- | ---------------- |
| `\d` | 숫자             |
| `\D` | 숫자가 아닌 문자 |
| `\w` | 영문자/숫자/`_`  |
| `\W` | `\w`가 아닌 문자 |
| `\s` | 공백 문자        |
| `\S` | 공백이 아닌 문자 |
| `\b` | 단어 경계        |
| `\B` | 단어 경계가 아님 |

단, 초보자는 다음 Cuff 패턴을 우선 사용할 수 있다.

```cuff
[num]
[str]
[sp]
```

즉:

```text
[num] ≈ \d
[str] ≈ \w - _
[sp]  ≈ 공백
```

`[str]`은 `_`를 포함하지 않는다는 점에 주의한다.

---

# 34. Unicode

Cuff Regex는 Unicode 문자열을 기본적으로 지원한다.

Unicode 속성 검사가 필요한 경우 `\p{...}`와 `\P{...}`를 사용한다.

```cuff
"\p{L}+"
```

Unicode 문자.

```cuff
"\p{N}+"
```

Unicode 숫자.

```cuff
"\p{Script=Hangul}+"
```

한글 문자.

부정:

```cuff
"\P{L}+"
```

Unicode 문자가 아닌 문자.

Unicode 속성 문법은 Unicode 표준 속성을 따른다.

---

# 35. Unicode Flag

Unicode Regex 기능은 `u` 플래그를 사용한다.

```cuff
match text with "\p{L}+" u
```

Cuff 런타임은 Unicode Regex를 기본적으로 지원해야 하며, `u`는 Unicode 속성 및 Unicode 코드포인트 관련 고급 동작을 명시한다.

---

# 36. Flags

Cuff는 JavaScript의 `/pattern/flags` 형태 대신 패턴 뒤에 짧은 flag를 붙인다.

```cuff
match value with "pattern" i
```

여러 flag는 연속해서 작성한다.

```cuff
match value with "pattern" gim
```

지원 flag:

| Flag | 의미          |
| ---- | ------------- |
| `i`  | 대소문자 무시 |
| `g`  | 모든 매칭     |
| `m`  | multiline     |
| `s`  | dotall        |
| `u`  | Unicode       |
| `y`  | sticky        |

---

# 37. `is` / `IS`와 `i` Flag

일반적인 조건식에서는 `IS`가 대소문자 무시를 담당한다.

```cuff
if value IS "[str]+" do:
    ...
end
```

따라서 단순한 검사에서는 `i` flag가 필요하지 않다.

`i`는 `match`, `find` 등의 명시적인 Regex API에서 사용할 수 있다.

```cuff
match value with "[str]+" i
```

---

# 38. Global Flag

`g`는 첫 번째 결과만 반환하지 않고 모든 매칭을 반환한다.

```cuff
find text with "[num]+" g
```

예:

```text
"abc123def456"
```

결과:

```text
123
456
```

`g`가 없으면 첫 번째 매칭만 반환한다.

---

# 39. Multiline Flag

`m`은 `^`, `$`의 동작을 각 줄 기준으로 변경한다.

```cuff
match text with "^abc$" m
```

`m`이 없으면 문자열 전체 기준이다.

---

# 40. Start / End Anchor

Cuff는 전체 문자열 매칭을 기본으로 하기 때문에 대부분의 경우 `^`, `$`가 필요 없다.

그러나 `find` 또는 `m`과 같은 부분 매칭 상황에서는 JavaScript 호환성을 위해 지원한다.

```cuff
"^abc"
"abc$"
"^abc$"
```

---

# 41. Atomic / Possessive 확장

Cuff Regex 1.0에서는 Atomic Group과 Possessive Quantifier를 기본 문법으로 정의하지 않는다.

따라서 다음은 예약 문법이다.

```text
(?>
++
*+
?+
```

향후 엔진 최적화 또는 고급 Regex 확장에서 추가할 수 있다.

---

# 42. 조건부 패턴

Regex 내부에서 실행 언어의 조건문을 호출하는 문법은 지원하지 않는다.

즉 다음과 같은 형태는 허용하지 않는다.

```text
(?(condition)yes|no)
```

Cuff는 Regex를 실행 코드와 분리한다.

복잡한 조건은 Cuff의 `if`를 사용한다.

---

# 43. Backtracking

Cuff Regex 엔진은 기본적으로 Backtracking 기반의 정규식 의미론을 따른다.

다음 요소는 Backtracking 대상이다.

```text
+
*
?
{N}
{N,M}
()
|
```

Lookaround 및 Backreference도 Backtracking 규칙에 영향을 받는다.

구현 엔진은 내부적으로 다른 알고리즘을 사용할 수 있지만 결과는 본 명세와 동일해야 한다.

---

# 44. 반복 횟수의 정확한 의미

다음:

```cuff
"[num]4"
```

는 정확히 4개의 `[num]`을 의미한다.

다음:

```cuff
"[num]2~5"
```

는 2, 3, 4, 5회 중 하나를 의미한다.

다음:

```cuff
"[num]2~"
```

는 2회 이상을 의미한다.

반복 횟수는 음수가 될 수 없다.

잘못된 예:

```cuff
"[num]-1"
"[num]0~"
"[num]5~2"
```

---

# 45. 숫자 반복 문법과 `~` 슬라이싱의 구분

CuffScript에서 `~`는 컬렉션 슬라이싱에도 사용된다.

```cuff
colors[2~3]
```

Regex 문자열 내부에서는 반복 범위를 의미한다.

```cuff
"[num]2~5"
```

두 문법은 서로 다른 파서 영역에서 해석되므로 충돌하지 않는다.

---

# 46. `:` 사용 규칙

CuffScript의 전역 콜론 규칙을 Regex에도 적용한다.

콜론 앞에는 공백을 둘 수 없다.

올바름:

```cuff
"[one:yes|no]"
"<name:[str]+>"
```

잘못됨:

```cuff
"[one :yes|no]"
"<name :[str]+>"
```

---

# 47. Regex와 일반 문자열의 구분

다음 문자열은 일반 문자열로 취급할 수 있는 형태다.

```cuff
"hello"
"hello world"
"010-1234"
```

Regex 타입 또는 Regex 메타문자가 포함된 경우 패턴으로 해석한다.

```cuff
"[num]4"
"[str]+"
"(abc)"
"[one:a|b]"
```

리터럴로 메타문자를 비교해야 하는 경우 Escape한다.

```cuff
"\[num\]4"
```

이는 실제 문자열:

```text
[num]4
```

와 매칭한다.

---

# 48. `[num]`과 `[str]`의 특수 해석

`[num]`과 `[str]`은 문자 집합이 아니라 Cuff의 기본 Pattern Token이다.

따라서:

```cuff
"[num]4"
```

는:

```text
숫자 4개
```

로 해석된다.

반면:

```cuff
"\[num\]4"
```

는:

```text
[num]4
```

라는 실제 문자열을 찾는다.

---

# 49. 중첩

Pattern Token은 그룹 및 반복과 함께 사용할 수 있다.

```cuff
"([num]-[num])+"
```

```cuff
"([one:A|B][num]2)+"
```

Named Capture도 중첩할 수 있다.

```cuff
"<id:(ID-[num]4)>"
```

다만 이름이 같은 Named Capture를 중복 선언하는 것은 금지한다.

잘못된 예:

```cuff
"<id:[num]2>-<id:[num]2>"
```

---

# 50. 이름 규칙

Named Capture의 이름은 Cuff 식별자 규칙을 따른다.

권장:

```text
year
month
day
user_id
name
```

금지:

```text
123
my-name
hello world
```

Named Capture 이름은 대소문자를 구분한다.

```text
<id:[num]+>
<ID:[num]+>
```

`id`와 `ID`는 서로 다른 이름이다.

---

# 51. Escape와 Cuff 문자열

Regex는 Cuff 문자열 내부에 존재하므로 문자열 Escape와 Regex Escape의 두 단계를 고려해야 한다.

예:

```cuff
"\\d+"
```

Cuff 문자열 해석 후 Regex 엔진에는:

```text
\d+
```

가 전달된다.

런타임은 문자열 Escape 처리 후 Regex Parser를 실행해야 한다.

---

# 52. Regex 오류

Regex 문법이 잘못된 경우 단순히 `false`를 반환하지 않는다.

다음은 `Regex Syntax Error`다.

```text
"[num]+4"
"[num]5~2"
"["
"(abc"
"[one:]"
"\"
```

런타임은 패턴 컴파일 단계에서 오류를 발생시킨다.

---

# 53. Runtime Regex Error

문법은 올바르지만 실행할 수 없는 경우 `Regex Runtime Error`가 발생한다.

예:

- 유효하지 않은 Unicode 속성
- 지원하지 않는 Regex 기능
- 잘못된 Backreference
- 엔진의 실행 제한 초과

---

# 54. Backreference 오류

존재하지 않는 캡처를 참조하면 오류다.

```cuff
"(abc)-\2"
```

첫 번째 캡처만 존재하므로 `\2`는 잘못된 참조다.

Named Capture도 동일하다.

```cuff
"<name:[str]+>-\k<id>"
```

`id`가 존재하지 않으므로 오류다.

---

# 55. 빈 패턴

빈 문자열 패턴은 빈 문자열과 매칭한다.

```cuff
""
```

다른 문자열에는 매칭하지 않는다.

`*`와 함께 사용할 경우 빈 문자열 매칭이 발생할 수 있다.

```cuff
"[num]*"
```

---

# 56. Regex 보안

Regex 엔진은 과도한 Backtracking으로 인해 실행 시간이 폭증할 수 있다.

런타임은 Regex 실행에 다음 제한을 둘 수 있다.

```text
Maximum Regex Steps
Maximum Regex Time
Maximum Capture Size
```

제한을 초과하면 `Regex Runtime Error`를 발생시킨다.

언어 구현체는 이를 무한 실행으로 방치해서는 안 된다.

---

# 57. 권장 기본 문법

초보자는 다음 문법만으로 대부분의 검증을 작성할 수 있다.

```cuff
[num]
[str]
[let]
[up]
[low]
[any]

+
*
?
N
N~M

[one:a|b|c]
[abc]
[a-z]

(abc)
<name:abc>
```

---

# 58. 예제 — PIN

```cuff
if pin is "[num]4" do:
    print("PIN 통과")
end
```

---

# 59. 예제 — 사용자 ID

```cuff
if user_id is "[str]3~20" do:
    print("ID 통과")
end
```

---

# 60. 예제 — 전화번호

```cuff
if phone is "010-[num]4-[num]4" do:
    print("올바른 번호")
end
```

---

# 61. 예제 — 이메일

간단한 이메일:

```cuff
if email IS "[str]+@[str]+.[str]2~10" do:
    print("이메일 형식")
end
```

`.`을 실제 문자로 사용하려면 Regex에서 `\.`을 사용한다.

```cuff
if email IS "[str]+@[str]+\.[str]2~10" do:
    print("이메일 형식")
end
```

실제 서비스 수준의 이메일 검증은 별도의 `[email]` 타입 패턴을 사용할 수 있다.

---

# 62. 예제 — 선택

```cuff
if status is "[one:ready|running|stopped]" do:
    print("상태 확인")
end
```

---

# 63. 예제 — 날짜

```cuff
if date is "<year:[num]4>-<month:[num]2>-<day:[num]2>" do:
    print("날짜 형식")
end
```

---

# 64. 예제 — 반복 그룹

```cuff
if code is "(AB-[num]2)+" do:
    print("코드 통과")
end
```

---

# 65. 예제 — 같은 값 반복

```cuff
if value is "([str]+)-\1" do:
    print("같은 문자열 반복")
end
```

---

# 66. 예제 — 대소문자 무시

```cuff
if command IS "[one:start|stop|pause]" do:
    print("명령 확인")
end
```

---

# 67. 예제 — 숫자 추출

```cuff
set match result to find text with "[num]+"

if result is not empty do:
    print(result[1])
end
```

`is not`은 별도의 Regex 문법이 아니라 Cuff의 논리 비교 문법으로 처리한다.

---

# 68. 예제 — Named Capture

```cuff
set match result to match date with "<year:[num]4>-<month:[num]2>-<day:[num]2>"

print(result["year"])
print(result["month"])
print(result["day"])
```

---

# 69. 예제 — Lookahead

```cuff
set match result to find text with "[num]+(?=원)"
```

`100원`에서:

```text
100
```

을 찾는다.

`원` 자체는 매칭 결과에 포함되지 않는다.

---

# 70. 예제 — Lookbehind

```cuff
set match result to find text with "(?<=USD)[num]+"
```

`USD100`에서:

```text
100
```

을 찾는다.

`USD`는 매칭 결과에 포함되지 않는다.

---

# 71. 예제 — 모든 숫자 찾기

```cuff
set match result to find text with "[num]+" g
```

---

# 72. 예제 — Unicode

```cuff
set match result to find text with "\p{L}+" u
```

Unicode 문자 단위로 검색한다.

---

# 73. 예제 — 여러 Flag

```cuff
set match result to find text with "^cuff" gim
```

의미:

```text
g = 모든 매칭
i = 대소문자 무시
m = 줄 단위 검사
```

---

# 74. JavaScript Regex와의 대응

Cuff는 JavaScript Regex의 개념을 다음과 같이 대응한다.

| JavaScript   | Cuff              |
| ------------ | ----------------- |
| `\d`         | `[num]`           |
| `\w`         | `[str]` 또는 `\w` |
| `\s`         | `[sp]` 또는 `\s`  |
| `.`          | `[any]`           |
| `{4}`        | `4`               |
| `{2,5}`      | `2~5`             |
| `{2,}`       | `2~`              |
| `?`          | `?`               |
| `+`          | `+`               |
| `*`          | `*`               |
| `(abc)`      | `(abc)`           |
| `(?:abc)`    | `(?:abc)`         |
| `a\|b`       | `[one:a\|b]`      |
| `[a-z]`      | `[a-z]`           |
| `[^a-z]`     | `[!a-z]`          |
| `(?=x)`      | `(?=x)`           |
| `(?!x)`      | `(?!x)`           |
| `(?<=x)`     | `(?<=x)`          |
| `(?<!x)`     | `(?<!x)`          |
| `\1`         | `\1`              |
| `(?<name>x)` | `<name:x>`        |
| `\k<name>`   | `\k<name>`        |
| `^`          | `^`               |
| `$`          | `$`               |
| `/i`         | `i` 또는 `IS`     |
| `/g`         | `g`               |
| `/m`         | `m`               |
| `/s`         | `s`               |
| `/u`         | `u`               |
| `/y`         | `y`               |

---

# 75. 설계 원칙

Cuff Regex는 다음 우선순위를 가진다.

### 1. 짧은 문법

```cuff
"[num]4"
```

는:

```regex
\d{4}
```

보다 우선한다.

### 2. 기존 Regex와의 호환성

고급 사용자는 다음과 같은 기존 Regex 개념을 사용할 수 있어야 한다.

```text
()
|
[]
{}
^
$
.
*
+
?
\
\b
\d
\w
\s
(?=)
(?!)
(?<=)
(?<!)
```

### 3. 의미 기반 문법

초보자는 기호를 몰라도 사용할 수 있어야 한다.

```cuff
[num]
[str]
[one:a|b]
```

### 4. 고급 기능은 짧게

복잡한 기능도 가능한 한 JavaScript보다 길어지지 않도록 한다.

---

# 76. 예약 문법

다음 문법은 현재 Regex 1.0에서 예약되어 있다.

```text
(?:
(?=
(?!
(?<=
(?<!
<name:
\k<
\p{
\P{
```

향후 기능 확장을 위해 다른 의미로 재사용해서는 안 된다.

---

# 77. 미지원 기능

Regex 1.0에서는 다음 기능을 필수로 제공하지 않는다.

```text
(?>...)
++
*+
?+
조건부 Regex
재귀 Regex
서브루틴 호출
```

이들은 향후 버전에서 추가할 수 있다.

---

# 78. 엔진 호환성 원칙

Cuff Regex 구현체가 내부적으로 JavaScript의 RegExp 엔진을 사용하더라도 Cuff 문법을 먼저 해석해야 한다.

처리 순서는 다음과 같다.

```text
Cuff Source
    ↓
Cuff String Parser
    ↓
Cuff Regex Parser
    ↓
Cuff Regex AST
    ↓
Runtime Regex Engine
```

구현체가 JavaScript가 아니더라도 최종 동작은 본 명세와 동일해야 한다.

---

# 79. 핵심 문법 요약

```text
[num]       숫자
[str]       영문자/숫자
[let]       영문자
[up]        대문자
[low]       소문자
[sp]        공백
[any]       임의 문자
[nl]        줄바꿈

N           정확히 N개
+           1개 이상
*           0개 이상
?           0개 또는 1개
N~M         N~M개
N~          N개 이상

[one:a|b]   a 또는 b
[abc]       a,b,c 중 하나
[a-z]       범위
[!a-z]      부정 범위

(...)       캡처 그룹
(?:...)     비캡처 그룹
<name:...>  이름 캡처

\1          캡처 참조
\k<name>    이름 캡처 참조

(?=...)     Lookahead
(?!...)     Negative Lookahead
(?<=...)    Lookbehind
(?<!...)    Negative Lookbehind

.           임의 문자
^           시작
$           끝
\b          단어 경계
\B          단어 경계 아님

\d          숫자
\D          숫자 아님
\w          word 문자
\W          word 문자 아님
\s          공백
\S          공백 아님

\p{...}     Unicode 속성
\P{...}     Unicode 속성 부정

i           대소문자 무시
g           전체 매칭
m           multiline
s           dotall
u           Unicode
y           sticky
```

---

# 80. 최종 예제

```cuff
note: Cuff Regex 종합 예제

set str phone to "010-1234-5678"
set str email to "Player_One@CuffLang.com"
set str code to "CUFF-2026"

if phone is "010-[num]4-[num]4" do:
    print("전화번호 통과")
end

if email IS "[str]+@[str]+\.[str]2~10" do:
    print("이메일 통과")
end

if code is "<name:CUFF>-[year:[num]4]" do:
    print(f"코드 이름: {name}")
    print(f"코드 연도: {year}")
end

set match result to find "price 100원, price 200원" with "[num]+(?=원)" g

print(result[1])
print(result[2])
```

---

# 81. 최종 철학

Cuff Regex는 정규식을 단순화하기 위해 기능을 제거하는 것이 아니다.

**JavaScript급 Regex의 표현력을 유지하면서, 자주 사용하는 기능을 Cuff의 짧은 타입 문법으로 압축하는 것을 목표로 한다.**

따라서:

```regex
\d{4}
```

보다:

```cuff
[num]4
```

를 권장하고,

```regex
[a-zA-Z0-9]+
```

보다:

```cuff
[str]+
```

를 권장한다.

하지만 고급 사용자는 다음과 같은 표현도 사용할 수 있어야 한다.

```cuff
"<id:[str]+>-(?=\d{4})[num]4"
```

즉 Cuff Regex의 핵심은:

```text
쉬운 입문
    +
짧은 문법
    +
높은 표현력
    +
JavaScript급 기능
```

이다.
