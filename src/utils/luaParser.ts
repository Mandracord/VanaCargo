export function luaToJson(text: string) {
  const normalized = text.replace(/^\uFEFF/, '');
  const noComments = normalized.replace(/^\s*--.*$/gm, '');
  const withoutReturn = noComments.replace(/^\s*return\s*/m, '').trim();
  const firstTable = (() => {
    const start = withoutReturn.indexOf('{');
    if (start === -1) throw new Error('No opening brace found');
    let depth = 0;
    for (let i = start; i < withoutReturn.length; i += 1) {
      const ch = withoutReturn[i];
      if (ch === '{') depth += 1;
      if (ch === '}') {
        depth -= 1;
        if (depth === 0) return withoutReturn.slice(start, i + 1);
      }
    }
    const end = withoutReturn.lastIndexOf('}');
    if (end > start) return withoutReturn.slice(start, end + 1);
    throw new Error('Unbalanced braces in Lua content');
  })();

  const cleaned = firstTable
    .replace(/}\s*(?="\d+":)/g, '},') // ensure commas between entries
    .replace(/"(?=\s*"\d+":\s*{)/g, '",') // ensure comma when next key follows directly
    .replace(/"(?=\d+":\s*{)/g, '",') // close missing quote before next key
    .replace(/(?<=[}\"])\s*"(?=\d+":\s*{)/g, ',"') // add missing comma between objects
    .replace(/\[(\d+)\]\s*=/g, '"$1":')
    .replace(/\["([^\"]+)"\]\s*=/g, '"$1":')
    .replace(/([A-Za-z_][A-Za-z0-9_]*)\s*=/g, '"$1":')
    .replace(/,(\s*[}\]])/g, '$1');

  const sanitized = cleaned.replace(/[\u0000-\u001f]/g, '');

  return JSON.parse(sanitized);
}
