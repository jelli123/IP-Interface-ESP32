"""Bracket and string balance check for the JavaScript inside src/index_html.h.

The dashboard is one big raw string literal; an unbalanced quote or brace
there does not break the build, it just kills the page silently.
"""
import re
import sys

OPEN = "([{"
CLOSE = ")]}"
PAIR = {")": "(", "]": "[", "}": "{"}

# A '/' starts a regex literal when the previous non-space character is one of
# these. '<' and '>' are deliberately absent: '=>' followed by a division would
# otherwise be read as a regex.
REGEX_PRE = set("(,=:[!&|?{};+-*%~^")


def check(path: str) -> int:
    src = open(path, encoding="utf-8").read()

    start = src.index("<script>")
    end = src.index("</script>", start)
    js = src[start + len("<script>"):end]

    stack = []
    i = 0
    line = 1
    prev = ""
    errors = []

    while i < len(js):
        c = js[i]

        if c == "\n":
            line += 1
            i += 1
            continue

        if c in "'\"`":
            quote = c
            i += 1
            while i < len(js) and js[i] != quote:
                if js[i] == "\\":
                    i += 1
                elif js[i] == "\n":
                    line += 1
                    if quote != "`":
                        errors.append(f"line {line}: newline inside {quote} string")
                i += 1
            if i >= len(js):
                errors.append(f"line {line}: unterminated {quote} string")
            i += 1
            prev = quote
            continue

        if c == "/" and i + 1 < len(js):
            if js[i + 1] == "/":
                while i < len(js) and js[i] != "\n":
                    i += 1
                continue
            if js[i + 1] == "*":
                i += 2
                while i + 1 < len(js) and not (js[i] == "*" and js[i + 1] == "/"):
                    if js[i] == "\n":
                        line += 1
                    i += 1
                i += 2
                continue
            if prev in REGEX_PRE or prev == "":
                i += 1
                while i < len(js) and js[i] != "/":
                    if js[i] == "\\":
                        i += 1
                    elif js[i] == "\n":
                        errors.append(f"line {line}: newline inside regex")
                        break
                    i += 1
                i += 1
                while i < len(js) and js[i].isalpha():
                    i += 1
                prev = "/"
                continue

        if c in OPEN:
            stack.append((c, line))
        elif c in CLOSE:
            if not stack:
                errors.append(f"line {line}: stray '{c}'")
            elif stack[-1][0] != PAIR[c]:
                o, ol = stack.pop()
                errors.append(f"line {line}: '{c}' closes '{o}' from line {ol}")
            else:
                stack.pop()

        if not c.isspace():
            prev = c
        i += 1

    for o, ol in stack:
        errors.append(f"line {ol}: '{o}' never closed")

    for e in errors:
        print(e)
    print("OK" if not errors else f"{len(errors)} problem(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(check(sys.argv[1] if len(sys.argv) > 1 else "src/index_html.h"))
