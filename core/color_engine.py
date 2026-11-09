# -*- coding: utf-8 -*-
"""着色引擎:标签B(设备端 [color:"..."] 标签)+ 规则C(正则规则)。
另含 HEX 字符串解析工具(发送区 / 工具箱共用)。"""
import re

NAMED_COLORS = {
    "red": "#FF5252", "green": "#00E676", "yellow": "#FFD600",
    "blue": "#448AFF", "cyan": "#00BCD4", "magenta": "#E040FB",
    "white": "#FFFFFF", "gray": "#9E9E9E", "orange": "#FF9100",
}

TAG_RE = re.compile(
    r'\[(' + "|".join(NAMED_COLORS) + r'|#[0-9A-Fa-f]{6}):"((?:\\.|[^"\\])*)"\]'
)


def compile_rules(rules):
    """把配置里的规则C编译为 [(compiled_regex, color), ...],非法正则跳过。"""
    out = []
    for r in rules:
        if not r.get("enabled", True):
            continue
        try:
            out.append((re.compile(r["pattern"]), r["color"]))
        except re.error:
            continue
    return out


def _apply_rules(text, compiled, out):
    """对一段普通文字应用规则C,追加 (text, color|None) 片段到 out。"""
    if not text:
        return
    if not compiled:
        out.append((text, None))
        return
    pos, n = 0, len(text)
    while pos < n:
        best = None
        for cre, color in compiled:
            m = cre.search(text, pos)
            if m and m.start() < m.end():
                if best is None or m.start() < best[0].start():
                    best = (m, color)
        if best is None:
            out.append((text[pos:], None))
            return
        m, color = best
        if m.start() > pos:
            out.append((text[pos:m.start()], None))
        out.append((m.group(0), color))
        pos = m.end()


def split_segments(text, compiled_rules):
    """返回 [(text, color|None), ...]。标签B优先级高于规则C,标签本身不显示。"""
    out = []
    pos = 0
    for m in TAG_RE.finditer(text):
        _apply_rules(text[pos:m.start()], compiled_rules, out)
        color = m.group(1)
        color = NAMED_COLORS.get(color, color)
        inner = m.group(2).replace('\\"', '"').replace("\\\\", "\\")
        if inner:
            out.append((inner, color))
        pos = m.end()
    _apply_rules(text[pos:], compiled_rules, out)
    return out


def parse_hex(text: str):
    """解析十六进制字符串("01 03 0A" / "01030A" / "0x01,0x03"),
    返回 bytes;非法返回 None。"""
    s = re.sub(r"0[xX]", " ", text)
    s = re.sub(r"[\s,;:\-]+", "", s)
    if not s:
        return b""
    if len(s) % 2 != 0 or not re.fullmatch(r"[0-9A-Fa-f]+", s):
        return None
    return bytes.fromhex(s)
