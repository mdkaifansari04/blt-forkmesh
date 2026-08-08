const RICH_TEXT_VERSION = 1;
const MAX_RICH_SOURCE = 16000;
const MAX_RENDER_NODES = 2048;
const SAFE_LINK_PROTOCOLS = new Set(["http:", "https:", "mailto:"]);
const MENTION_RE = /^@([a-z](?:[a-z0-9-]{0,61}[a-z0-9])?)\b/i;

function normalizedSource(value) {
  return String(value ?? "")
    .replace(/\r\n?/g, "\n")
    .replace(/\0/g, "")
    .slice(0, MAX_RICH_SOURCE);
}

function boundedRange(value, start, end) {
  const length = value.length;
  const first = Math.max(0, Math.min(length, Number(start) || 0));
  const second = Math.max(first, Math.min(length, Number(end) || first));
  return [first, second];
}

function safeLink(value) {
  try {
    const url = new URL(String(value || "").trim(), window.location.href);
    return SAFE_LINK_PROTOCOLS.has(url.protocol) ? url.href : "";
  } catch (_) {
    return "";
  }
}

function appendText(parent, value, state) {
  if (!value) return;
  if (state.nodes >= MAX_RENDER_NODES) return;
  parent.append(document.createTextNode(value));
  state.nodes += 1;
}

function appendLink(parent, label, href, state) {
  if (state.nodes >= MAX_RENDER_NODES) return;
  const link = document.createElement("a");
  link.href = href;
  link.target = "_blank";
  link.rel = "noopener noreferrer";
  link.append(document.createTextNode(label));
  parent.append(link);
  state.nodes += 2;
}

function markerAt(source, index) {
  const pairs = [
    ["**", "**", "strong"],
    ["++", "++", "u"],
    ["~~", "~~", "s"],
    ["`", "`", "code"],
  ];
  for (const pair of pairs) {
    if (source.startsWith(pair[0], index)) return pair;
  }
  if (source[index] === "*" && source[index + 1] !== "*") {
    return ["*", "*", "em"];
  }
  return null;
}

function rawUrlAt(source, index) {
  const match = source.slice(index).match(/^https?:\/\/[^\s<>]+/i);
  if (!match) return "";
  return match[0].replace(/[),.!?;:]+$/g, "");
}

function linkCloseAt(source, start) {
  let depth = 0;
  for (let index = start; index < source.length; index += 1) {
    if (source[index] === "(") depth += 1;
    if (source[index] !== ")") continue;
    if (depth === 0) return index;
    depth -= 1;
  }
  return -1;
}

function appendInline(parent, source, options, state, depth = 0) {
  if (!source || state.nodes >= MAX_RENDER_NODES) return;
  if (depth > 8) {
    appendText(parent, source, state);
    return;
  }

  let index = 0;
  let plain = "";
  const flushPlain = () => {
    appendText(parent, plain, state);
    plain = "";
  };

  while (index < source.length && state.nodes < MAX_RENDER_NODES) {
    if (source[index] === "\\" && index + 1 < source.length) {
      plain += source[index + 1];
      index += 2;
      continue;
    }

    if (source[index] === "[") {
      const labelEnd = source.indexOf("](", index + 1);
      const linkEnd = labelEnd >= 0 ? linkCloseAt(source, labelEnd + 2) : -1;
      if (labelEnd > index + 1 && linkEnd > labelEnd + 2) {
        flushPlain();
        const label = source.slice(index + 1, labelEnd);
        const href = safeLink(source.slice(labelEnd + 2, linkEnd));
        if (href) {
          const link = document.createElement("a");
          link.href = href;
          link.target = "_blank";
          link.rel = "noopener noreferrer";
          parent.append(link);
          state.nodes += 1;
          appendInline(link, label, options, state, depth + 1);
        } else {
          appendInline(parent, label, options, state, depth + 1);
        }
        index = linkEnd + 1;
        continue;
      }
    }

    const marker = markerAt(source, index);
    if (marker) {
      const close = source.indexOf(marker[1], index + marker[0].length);
      if (close > index + marker[0].length) {
        flushPlain();
        const element = document.createElement(marker[2]);
        parent.append(element);
        state.nodes += 1;
        const content = source.slice(index + marker[0].length, close);
        if (marker[2] === "code") appendText(element, content, state);
        else appendInline(element, content, options, state, depth + 1);
        index = close + marker[1].length;
        continue;
      }
    }

    const rawUrl = rawUrlAt(source, index);
    if (rawUrl) {
      flushPlain();
      const href = safeLink(rawUrl);
      if (href) appendLink(parent, rawUrl, href, state);
      else appendText(parent, rawUrl, state);
      index += rawUrl.length;
      continue;
    }

    if (source[index] === "@") {
      const previous = index > 0 ? source[index - 1] : "";
      const mention = source.slice(index).match(MENTION_RE);
      if (mention && !/[A-Za-z0-9_-]/.test(previous)) {
        flushPlain();
        const mentionText = mention[0];
        const rendered = options.renderMention?.(parent, mentionText, mention[1]);
        if (rendered instanceof Node) parent.append(rendered);
        if (!rendered) appendText(parent, mentionText, state);
        else state.nodes += 1;
        index += mentionText.length;
        continue;
      }
    }

    plain += source[index];
    index += 1;
  }
  flushPlain();
}

function appendLineBreak(parent, state) {
  if (state.nodes >= MAX_RENDER_NODES) return;
  parent.append(document.createElement("br"));
  state.nodes += 1;
}

export function normalizeRichText(value) {
  if (typeof value === "string") {
    return { v: RICH_TEXT_VERSION, source: normalizedSource(value) };
  }
  if (!value || typeof value !== "object" || value.v !== RICH_TEXT_VERSION) {
    return null;
  }
  return { v: RICH_TEXT_VERSION, source: normalizedSource(value.source) };
}

export function plainTextFromRichSource(source) {
  return normalizedSource(source)
    .replace(/\[([^\]]+)\]\([^\s)]+(?:\s+[^)]*)?\)/g, "$1")
    .replace(/\*\*([^*]+)\*\*/g, "$1")
    .replace(/\+\+([^+]+)\+\+/g, "$1")
    .replace(/~~([^~]+)~~/g, "$1")
    .replace(/`([^`]+)`/g, "$1")
    .replace(/\*([^*]+)\*/g, "$1")
    .replace(/^(?:-\s+|\d+\.\s+|>\s?)/gm, "")
    .replace(/\\([\\`*+~[\]()])/g, "$1");
}

export function wrapSelection(value, start, end, before, after) {
  const source = String(value ?? "");
  const opening = String(before ?? "");
  const closing = String(after ?? "");
  const [selectionStart, selectionEnd] = boundedRange(source, start, end);
  const selected = source.slice(selectionStart, selectionEnd);
  return {
    value: `${source.slice(0, selectionStart)}${opening}${selected}${closing}${source.slice(selectionEnd)}`,
    selectionStart: selectionStart + opening.length,
    selectionEnd: selectionEnd + opening.length,
  };
}

export function toggleLinePrefix(value, start, end, prefix) {
  const source = String(value ?? "");
  const token = String(prefix ?? "");
  const [selectionStart, selectionEnd] = boundedRange(source, start, end);
  const firstLine = source.lastIndexOf("\n", Math.max(0, selectionStart - 1)) + 1;
  const selectedEnd = selectionEnd === firstLine
    ? selectionEnd
    : source.indexOf("\n", selectionEnd) < 0
      ? source.length
      : source.indexOf("\n", selectionEnd);
  const block = source.slice(firstLine, selectedEnd);
  const lines = block.split("\n");
  const remove = Boolean(token) && lines.every((line) => line.startsWith(token));
  const transformed = lines
    .map((line) => remove ? line.slice(token.length) : `${token}${line}`)
    .join("\n");
  const deltaPerLine = remove ? -token.length : token.length;
  return {
    value: `${source.slice(0, firstLine)}${transformed}${source.slice(selectedEnd)}`,
    selectionStart: Math.max(firstLine, selectionStart + deltaPerLine),
    selectionEnd: Math.max(firstLine, selectionEnd + (deltaPerLine * lines.length)),
  };
}

export function linkSelection(value, start, end) {
  const source = String(value ?? "");
  const [selectionStart, selectionEnd] = boundedRange(source, start, end);
  const selected = source.slice(selectionStart, selectionEnd) || "link text";
  const replacement = `[${selected}](https://)`;
  const urlStart = selectionStart + selected.length + 3;
  return {
    value: `${source.slice(0, selectionStart)}${replacement}${source.slice(selectionEnd)}`,
    selectionStart: urlStart,
    selectionEnd: urlStart + 8,
  };
}

export function renderRichText(container, message, options = {}) {
  if (!container) return;
  const richText = normalizeRichText(message?.richText);
  const source = richText ? richText.source : normalizedSource(message?.text);
  container.replaceChildren();
  const state = { nodes: 0 };
  const lines = source.split("\n");

  for (let index = 0; index < lines.length;) {
    const line = lines[index];
    const bullet = line.match(/^-\s+(.*)$/);
    const numbered = line.match(/^\d+\.\s+(.*)$/);
    const quote = line.match(/^>\s?(.*)$/);

    if (bullet || numbered) {
      const list = document.createElement(bullet ? "ul" : "ol");
      container.append(list);
      state.nodes += 1;
      const listPattern = bullet ? /^-\s+(.*)$/ : /^\d+\.\s+(.*)$/;
      while (index < lines.length) {
        const item = lines[index].match(listPattern);
        if (!item) break;
        const row = document.createElement("li");
        list.append(row);
        state.nodes += 1;
        appendInline(row, item[1], options, state);
        index += 1;
      }
      continue;
    }

    if (quote) {
      const blockquote = document.createElement("blockquote");
      container.append(blockquote);
      state.nodes += 1;
      while (index < lines.length) {
        const quoted = lines[index].match(/^>\s?(.*)$/);
        if (!quoted) break;
        if (blockquote.childNodes.length) appendLineBreak(blockquote, state);
        appendInline(blockquote, quoted[1], options, state);
        index += 1;
      }
      continue;
    }

    appendInline(container, line, options, state);
    index += 1;
    if (index < lines.length) appendLineBreak(container, state);
  }
}
