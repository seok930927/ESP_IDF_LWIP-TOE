# -*- coding: utf-8 -*-
import sys, re, html

def conv(md):
    lines = md.split('\n')
    out = []
    i = 0
    in_code = False
    code_buf = []
    def flush_table(tbl):
        # tbl: list of raw "| a | b |" lines (first row header, second is ---)
        rows = []
        for r in tbl:
            cells = [c.strip() for c in r.strip().strip('|').split('|')]
            rows.append(cells)
        if len(rows) < 2:
            return ''
        head = rows[0]
        body = rows[2:]
        h = '<table><thead><tr>' + ''.join('<th>%s</th>' % inline(c) for c in head) + '</tr></thead><tbody>'
        for br in body:
            h += '<tr>' + ''.join('<td>%s</td>' % inline(c) for c in br) + '</tr>'
        h += '</tbody></table>'
        return h
    def inline(t):
        t = html.escape(t)
        t = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', t)
        t = re.sub(r'`([^`]+?)`', r'<code>\1</code>', t)
        t = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'\1', t)  # drop links, keep text
        return t
    while i < len(lines):
        line = lines[i]
        if line.strip().startswith('```'):
            if not in_code:
                in_code = True; code_buf = []
            else:
                in_code = False
                out.append('<pre><code>' + html.escape('\n'.join(code_buf)) + '</code></pre>')
            i += 1; continue
        if in_code:
            code_buf.append(line); i += 1; continue
        # table
        if line.strip().startswith('|') and i+1 < len(lines) and re.match(r'^\s*\|[\s:|-]+\|\s*$', lines[i+1]):
            tbl = []
            while i < len(lines) and lines[i].strip().startswith('|'):
                tbl.append(lines[i]); i += 1
            out.append(flush_table(tbl)); continue
        m = re.match(r'^(#{1,6})\s+(.*)$', line)
        if m:
            lvl = len(m.group(1)); out.append('<h%d>%s</h%d>' % (lvl, inline(m.group(2)), lvl)); i += 1; continue
        if re.match(r'^\s*---\s*$', line):
            out.append('<hr>'); i += 1; continue
        if line.strip().startswith('>'):
            out.append('<blockquote>%s</blockquote>' % inline(line.strip()[1:].strip())); i += 1; continue
        if re.match(r'^\s*[-*]\s+', line):
            items = []
            while i < len(lines) and re.match(r'^\s*[-*]\s+', lines[i]):
                items.append('<li>%s</li>' % inline(re.sub(r'^\s*[-*]\s+', '', lines[i]))); i += 1
            out.append('<ul>' + ''.join(items) + '</ul>'); continue
        if re.match(r'^\s*\d+\.\s+', line):
            items = []
            while i < len(lines) and re.match(r'^\s*\d+\.\s+', lines[i]):
                items.append('<li>%s</li>' % inline(re.sub(r'^\s*\d+\.\s+', '', lines[i]))); i += 1
            out.append('<ol>' + ''.join(items) + '</ol>'); continue
        if line.strip() == '':
            i += 1; continue
        out.append('<p>%s</p>' % inline(line)); i += 1
    return '\n'.join(out)

css = """
@page { size: A4; margin: 16mm 14mm; }
* { font-family: 'Malgun Gothic','맑은 고딕',sans-serif; }
body { font-size: 11px; line-height: 1.5; color:#1a1a1a; }
h1 { font-size: 22px; border-bottom:3px solid #2c5; padding-bottom:6px; }
h2 { font-size: 17px; border-bottom:1px solid #ccc; padding-bottom:4px; margin-top:22px; }
h3 { font-size: 14px; margin-top:16px; }
table { border-collapse: collapse; width:100%; margin:8px 0; font-size:10px; }
th,td { border:1px solid #bbb; padding:4px 7px; text-align:left; vertical-align:top; }
th { background:#eef6ee; }
code { background:#f0f0f0; padding:1px 4px; border-radius:3px; font-family:Consolas,monospace; font-size:10px; }
pre { background:#1e1e1e; color:#e8e8e8; padding:10px; border-radius:5px; overflow:auto; }
pre code { background:none; color:#e8e8e8; font-size:9.5px; white-space:pre; }
blockquote { border-left:4px solid #2c5; background:#f6fff6; margin:8px 0; padding:6px 12px; color:#333; }
hr { border:none; border-top:1px solid #ddd; margin:18px 0; }
ul,ol { margin:6px 0 6px 18px; }
strong { color:#000; }
"""

src, dst = sys.argv[1], sys.argv[2]
with open(src, encoding='utf-8') as f:
    body = conv(f.read())
htmldoc = '<!doctype html><html><head><meta charset="utf-8"><style>%s</style></head><body>%s</body></html>' % (css, body)
with open(dst, 'w', encoding='utf-8') as f:
    f.write(htmldoc)
print('wrote', dst)
