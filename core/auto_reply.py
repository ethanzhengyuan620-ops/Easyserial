# -*- coding: utf-8 -*-
"""按接收帧匹配规则并生成自动回复字节。"""
import re

from .color_engine import parse_hex

LINE_ENDINGS = [b"", b"\r\n", b"\r", b"\n"]
MATCH_MODES = {"exact", "contains", "regex"}


def _ascii_source(data: bytes) -> str:
    # 文本设备常把 CR/LF 当帧结束符，匹配内容时默认不要求用户重复填写。
    return data.decode("utf-8", errors="replace").rstrip("\r\n")


def _hex_source(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def validate_rule(rule: dict):
    """校验一条规则；合法时返回 None，否则返回可直接显示的错误文本。"""
    rx_format = str(rule.get("rx_format", "ASCII")).upper()
    tx_format = str(rule.get("tx_format", "ASCII")).upper()
    match = rule.get("match", "exact")
    pattern = str(rule.get("pattern", ""))
    reply = str(rule.get("reply", ""))

    if rx_format not in ("ASCII", "HEX"):
        return "接收格式必须是 ASCII 或 HEX"
    if tx_format not in ("ASCII", "HEX"):
        return "回复格式必须是 ASCII 或 HEX"
    if match not in MATCH_MODES:
        return "不支持的匹配方式"
    if not pattern:
        return "接收匹配内容不能为空"
    if not reply:
        return "回复内容不能为空"

    if match == "regex":
        try:
            re.compile(pattern)
        except re.error as e:
            return f"正则表达式非法：{e}"
    elif rx_format == "HEX":
        parsed = parse_hex(pattern)
        if parsed is None or not parsed:
            return "HEX 接收匹配内容非法"

    if tx_format == "HEX":
        parsed = parse_hex(reply)
        if parsed is None or not parsed:
            return "HEX 回复内容非法"
    return None


def _matches(data: bytes, rule: dict) -> bool:
    rx_format = str(rule.get("rx_format", "ASCII")).upper()
    match = rule.get("match", "exact")
    pattern = str(rule.get("pattern", ""))

    if rx_format == "HEX" and match != "regex":
        expected = parse_hex(pattern)
        return data == expected if match == "exact" else expected in data

    source = _ascii_source(data) if rx_format == "ASCII" else _hex_source(data)
    if match == "regex":
        flags = 0 if rule.get("case_sensitive", True) else re.IGNORECASE
        return re.search(pattern, source, flags) is not None

    if not rule.get("case_sensitive", True):
        source, pattern = source.casefold(), pattern.casefold()
    return source == pattern if match == "exact" else pattern in source


def _build_payload(rule: dict) -> bytes:
    reply = str(rule.get("reply", ""))
    if str(rule.get("tx_format", "ASCII")).upper() == "HEX":
        # HEX 回复始终保持原始字节，不追加 CR/LF。
        return parse_hex(reply)
    ending = int(rule.get("line_ending", 0))
    if not 0 <= ending < len(LINE_ENDINGS):
        ending = 0
    return reply.encode("utf-8") + LINE_ENDINGS[ending]


def find_auto_reply(data: bytes, rules):
    """返回 (payload, rule_name, error)。按规则列表顺序命中第一条。"""
    first_error = None
    for index, rule in enumerate(rules or []):
        if not rule.get("enabled", True):
            continue
        error = validate_rule(rule)
        if error:
            if first_error is None:
                first_error = f"规则 {index + 1}：{error}"
            continue
        if _matches(data, rule):
            name = rule.get("name", "").strip() or f"规则 {index + 1}"
            return _build_payload(rule), name, None
    return None, None, first_error
