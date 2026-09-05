# Contributing to CuffScript

Thank you for contributing to CuffScript.

---

## Before You Start

- Check the [issue tracker](https://github.com/cuffscript/cuffscript/issues) to see if the same topic has already been reported.
- Before contributing code, please read the relevant documentation to understand the language design.

---

## Reporting Issues

If you find a bug, please open an issue and include the following:

- A minimal CuffScript code snippet that reproduces the problem
- The actual output and the expected output
- Environment details such as OS and compiler version

If you would like to propose a new feature, please open an issue first before writing any code.
Pull requests for large features submitted without prior discussion may be closed.

---

## Pull Requests

1. Fork the repository and create a working branch.
2. Make your changes.
3. Verify that the build succeeds with `make`.
4. Submit the PR and reference the related issue number in the description.

If your change affects the language grammar or token system, please also update the relevant specification documents (`docs/SPEC.md` or `docs/REGEX.md`).

---

## Code Style

- Use the C++17 standard.
- Use 4 spaces for indentation.
- Do not use `using namespace std;` in header files.

---

## Building

```bash
make
./cuffc path/to/program.cuff

---

# CuffScript 프로젝트에 기여하기 (한국어)

CuffScript에 기여해 주셔서 감사합니다.
---

## 시작하기 전에

- [이슈 목록](https://github.com/cuffscript/cuffscript/issues)에서 동일한 내용이 이미 등록되어 있는지 먼저 확인해 주세요.
- 코드 기여 전에 관련 문서들을 읽고 언어 설계 의도를 파악해 주세요.

---

## 이슈 제보

버그를 발견했다면 이슈 트래커에 아래 내용을 포함하여 등록해 주세요.

- 문제를 재현할 수 있는 최소한의 CuffScript 코드
- 실제 출력 결과와 기대 출력 결과
- OS, 컴파일러 버전 등 환경 정보

새로운 기능을 제안하고 싶다면 구현 전에 먼저 이슈로 제안해 주세요.  
논의 없이 제출된 대규모 기능 PR은 반려될 수 있습니다.

---

## 풀 리퀘스트

1. 저장소를 포크하고 작업 브랜치를 만듭니다.
2. 변경 사항을 작성합니다.
3. `make`로 빌드가 성공하는지 확인합니다.
4. PR을 제출하고 관련 이슈 번호를 본문에 명시합니다.

언어 문법이나 토큰 체계를 변경하는 경우 관련 명세 문서(`docs/SPEC.md` 또는 `docs/REGEX.md`) 업데이트를 함께 포함해 주세요.

---

## 코드 스타일

- C++17 표준을 사용합니다.
- 들여쓰기는 공백 4칸을 사용합니다.
- `using namespace std;`는 헤더 파일에서 사용하지 않습니다.

---

## 빌드 방법

```bash
make
./cuffc path/to/program.cuff
```
