// terminal.js -- ANSI terminal renderer for the output pane.
// Parses ANSI SGR escape sequences and renders colored HTML spans.
'use strict';

const XTERM_COLORS = [
    // Standard 16 colors
    '#000000','#aa0000','#00aa00','#aa5500','#0000aa','#aa00aa','#00aaaa','#aaaaaa',
    '#555555','#ff5555','#55ff55','#ffff55','#5555ff','#ff55ff','#55ffff','#ffffff',
];
// Generate 216 color cube (indices 16-231)
for (let r = 0; r < 6; r++)
    for (let g = 0; g < 6; g++)
        for (let b = 0; b < 6; b++)
            XTERM_COLORS.push(`#${(r?r*40+55:0).toString(16).padStart(2,'0')}${(g?g*40+55:0).toString(16).padStart(2,'0')}${(b?b*40+55:0).toString(16).padStart(2,'0')}`);
// Grayscale ramp (indices 232-255)
for (let i = 0; i < 24; i++) {
    const v = (i * 10 + 8).toString(16).padStart(2, '0');
    XTERM_COLORS.push(`#${v}${v}${v}`);
}

function escapeHtml(text) {
    return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function renderAnsiLine(text) {
    // Parse ANSI SGR sequences and emit HTML spans.
    let result = '';
    let fg = null, bg = null;
    let bold = false, underline = false, inverse = false;
    let i = 0;

    function openSpan() {
        let style = '';
        let efg = inverse ? bg : fg;
        let ebg = inverse ? fg : bg;
        if (efg) style += `color:${efg};`;
        if (ebg) style += `background:${ebg};`;
        if (bold) style += 'font-weight:bold;';
        if (underline) style += 'text-decoration:underline;';
        if (style) return `<span style="${style}">`;
        return '';
    }

    let spanOpen = false;

    while (i < text.length) {
        if (text[i] === '\x1b' && text[i + 1] === '[') {
            // Parse CSI sequence
            i += 2;
            let params = '';
            while (i < text.length && text[i] >= '0' && text[i] <= ';') {
                params += text[i++];
            }
            const finalByte = text[i++]; // should be 'm' for SGR

            if (finalByte === 'm') {
                if (spanOpen) { result += '</span>'; spanOpen = false; }

                const codes = params ? params.split(';').map(Number) : [0];
                // Helper: truthy only for a finite integer in [0, 255].
                // Rejects undefined (short sequence) and NaN (non-numeric).
                const isByte = (v) => Number.isInteger(v) && v >= 0 && v <= 255;

                for (let j = 0; j < codes.length; j++) {
                    const c = codes[j];
                    if (c === 0) { fg = null; bg = null; bold = false; underline = false; inverse = false; }
                    else if (c === 1) bold = true;
                    else if (c === 4) underline = true;
                    else if (c === 7) inverse = true;
                    else if (c === 22) bold = false;
                    else if (c === 24) underline = false;
                    else if (c === 27) inverse = false;
                    else if (c >= 30 && c <= 37) fg = XTERM_COLORS[c - 30 + (bold ? 8 : 0)];
                    else if (c === 38 && codes[j+1] === 5) {
                        // \e[38;5;N m — 256-color palette index.
                        if (isByte(codes[j+2])) {
                            fg = XTERM_COLORS[codes[j+2]] || null;
                            j += 2;
                        } else {
                            j = codes.length; // malformed — stop parsing
                        }
                    }
                    else if (c === 38 && codes[j+1] === 2) {
                        // \e[38;2;R;G;B m — 24-bit truecolor.
                        if (isByte(codes[j+2]) && isByte(codes[j+3]) && isByte(codes[j+4])) {
                            fg = `rgb(${codes[j+2]},${codes[j+3]},${codes[j+4]})`;
                            j += 4;
                        } else {
                            j = codes.length;
                        }
                    }
                    else if (c === 39) fg = null;
                    else if (c >= 40 && c <= 47) bg = XTERM_COLORS[c - 40];
                    else if (c === 48 && codes[j+1] === 5) {
                        if (isByte(codes[j+2])) {
                            bg = XTERM_COLORS[codes[j+2]] || null;
                            j += 2;
                        } else {
                            j = codes.length;
                        }
                    }
                    else if (c === 48 && codes[j+1] === 2) {
                        if (isByte(codes[j+2]) && isByte(codes[j+3]) && isByte(codes[j+4])) {
                            bg = `rgb(${codes[j+2]},${codes[j+3]},${codes[j+4]})`;
                            j += 4;
                        } else {
                            j = codes.length;
                        }
                    }
                    else if (c === 49) bg = null;
                    else if (c >= 90 && c <= 97) fg = XTERM_COLORS[c - 90 + 8];
                    else if (c >= 100 && c <= 107) bg = XTERM_COLORS[c - 100 + 8];
                }
            }
        } else {
            if (!spanOpen && (fg || bg || bold || underline || inverse)) {
                result += openSpan();
                spanOpen = true;
            }
            result += escapeHtml(text[i]);
            i++;
        }
    }

    if (spanOpen) result += '</span>';
    return result || '&nbsp;';
}
