/*
 * KDatalogger CSV parser.
 *
 * Reads the log written by logger_service.c (UTF-8 BOM, ';' separator, ','
 * decimal mark, "Data;Ora;Tempo [s]" then "<name> [<unit>]" per channel,
 * empty cell = invalid reading) and is lenient with what Excel may turn it
 * into after a re-save: ',' or tab separators, '.' decimals, quoted cells,
 * Windows-1252 text (handled by the caller's decoder).
 *
 * Shared by the page (window.KdlCsv) and the node test (module.exports).
 */
(function (root) {
    'use strict';

    var TIME_NAMES = /^(tempo|time)\b/i;

    function stripBom(text) {
        return text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
    }

    function detectSeparator(headerLine) {
        var counts = { ';': 0, '\t': 0, ',': 0 };
        for (var i = 0; i < headerLine.length; i++) {
            var c = headerLine[i];
            if (c in counts) {
                counts[c]++;
            }
        }
        if (counts[';'] > 0) {
            return ';';
        }
        return counts['\t'] > 0 ? '\t' : ',';
    }

    /** Split one line, honouring double-quoted cells ("" is a literal quote). */
    function splitLine(line, sep) {
        if (line.indexOf('"') < 0) {
            return line.split(sep);
        }
        var cells = [];
        var cell = '';
        var quoted = false;
        for (var i = 0; i < line.length; i++) {
            var c = line[i];
            if (quoted) {
                if (c === '"' && line[i + 1] === '"') {
                    cell += '"';
                    i++;
                } else if (c === '"') {
                    quoted = false;
                } else {
                    cell += c;
                }
            } else if (c === '"') {
                quoted = true;
            } else if (c === sep) {
                cells.push(cell);
                cell = '';
            } else {
                cell += c;
            }
        }
        cells.push(cell);
        return cells;
    }

    /** "Cil 1 [°C]" -> { name: "Cil 1", unit: "°C" }. */
    function parseHeaderCell(text) {
        var trimmed = text.trim();
        var match = /^(.*?)\s*\[([^\]]*)\]\s*$/.exec(trimmed);
        if (match) {
            return { name: match[1].trim(), unit: match[2].trim() };
        }
        return { name: trimmed, unit: '' };
    }

    /**
     * Number from a cell, or null when empty/unparseable. When the cell has a
     * ',' decimal mark, any '.' is a thousands separator (Excel may add them);
     * without a ',' a '.' is taken as the decimal mark.
     */
    function parseNumber(text, decimalComma) {
        var s = text.trim();
        if (s === '') {
            return null;
        }
        if (decimalComma && s.indexOf(',') >= 0) {
            s = s.replace(/\./g, '').replace(',', '.');
        }
        var value = Number(s);
        return isFinite(value) ? value : null;
    }

    /**
     * Parse a whole file.
     * @returns {{
     *   name: string, time: number[], columns: {name, unit, label, values: (number|null)[],
     *   validCount: number}[], rowCount: number, startDate: string, startTime: string,
     *   periodS: number|null, durationS: number, warnings: string[]
     * }}
     * @throws Error with an Italian message when the file is not a usable log.
     */
    function parse(text, fileName) {
        var lines = stripBom(text).split(/\r\n|\n|\r/);
        while (lines.length > 0 && lines[lines.length - 1].trim() === '') {
            lines.pop();
        }
        if (lines.length < 2) {
            throw new Error('il file è vuoto o contiene solo l\'intestazione');
        }

        var sep = detectSeparator(lines[0]);
        var decimalComma = sep !== ',';
        var header = splitLine(lines[0], sep).map(parseHeaderCell);

        var timeIndex = -1;
        var dateIndex = -1;
        var clockIndex = -1;
        for (var h = 0; h < header.length; h++) {
            var lower = header[h].name.toLowerCase();
            if (timeIndex < 0 && TIME_NAMES.test(header[h].name)) {
                timeIndex = h;
            } else if (lower === 'data' || lower === 'date') {
                dateIndex = h;
            } else if (lower === 'ora' || lower === 'time of day') {
                clockIndex = h;
            }
        }
        if (timeIndex < 0) {
            throw new Error('manca la colonna "Tempo [s]": non sembra un log del datalogger');
        }

        var columns = [];
        var columnIndex = [];
        for (var c = 0; c < header.length; c++) {
            if (c === timeIndex || c === dateIndex || c === clockIndex || header[c].name === '') {
                continue;
            }
            var unit = header[c].unit;
            columns.push({
                name: header[c].name,
                unit: unit,
                label: unit ? header[c].name + ' [' + unit + ']' : header[c].name,
                values: [],
                validCount: 0,
            });
            columnIndex.push(c);
        }
        if (columns.length === 0) {
            throw new Error('nessun canale trovato nell\'intestazione');
        }

        var warnings = [];
        var time = [];
        var skipped = 0;
        var startDate = '';
        var startTime = '';
        for (var r = 1; r < lines.length; r++) {
            if (lines[r].trim() === '') {
                continue;
            }
            var cells = splitLine(lines[r], sep);
            var t = parseNumber(cells[timeIndex] || '', decimalComma);
            if (t === null) {
                skipped++;
                continue;
            }
            if (time.length === 0) {
                startDate = dateIndex >= 0 ? (cells[dateIndex] || '').trim() : '';
                startTime = clockIndex >= 0 ? (cells[clockIndex] || '').trim() : '';
            }
            time.push(t);
            for (var k = 0; k < columns.length; k++) {
                var v = parseNumber(cells[columnIndex[k]] || '', decimalComma);
                columns[k].values.push(v);
                if (v !== null) {
                    columns[k].validCount++;
                }
            }
        }
        if (time.length === 0) {
            throw new Error('nessuna riga con un tempo valido');
        }
        if (skipped > 0) {
            warnings.push(skipped + (skipped === 1 ? ' riga ignorata' : ' righe ignorate') +
                          ' (tempo mancante o non numerico)');
        }

        sortByTime(time, columns, warnings);

        return {
            name: fileName || '',
            time: time,
            columns: columns,
            rowCount: time.length,
            startDate: startDate,
            startTime: startTime,
            periodS: medianStep(time),
            durationS: time[time.length - 1] - time[0],
            warnings: warnings,
        };
    }

    /** The chart needs ascending x: reorder rows if the file is not already sorted. */
    function sortByTime(time, columns, warnings) {
        var sorted = true;
        for (var i = 1; i < time.length; i++) {
            if (time[i] < time[i - 1]) {
                sorted = false;
                break;
            }
        }
        if (sorted) {
            return;
        }
        var order = time.map(function (_, idx) { return idx; });
        order.sort(function (a, b) { return time[a] - time[b] || a - b; });
        var t2 = order.map(function (idx) { return time[idx]; });
        for (var j = 0; j < t2.length; j++) {
            time[j] = t2[j];
        }
        columns.forEach(function (col) {
            var v2 = order.map(function (idx) { return col.values[idx]; });
            col.values = v2;
        });
        warnings.push('righe non in ordine di tempo: riordinate');
    }

    /** Typical sample period: the median step, immune to the odd late sample. */
    function medianStep(time) {
        if (time.length < 2) {
            return null;
        }
        var steps = [];
        var limit = Math.min(time.length, 2001);
        for (var i = 1; i < limit; i++) {
            steps.push(time[i] - time[i - 1]);
        }
        steps.sort(function (a, b) { return a - b; });
        return steps[Math.floor(steps.length / 2)];
    }

    var api = { parse: parse, parseNumber: parseNumber, parseHeaderCell: parseHeaderCell,
                splitLine: splitLine, detectSeparator: detectSeparator };

    if (typeof module !== 'undefined' && module.exports) {
        module.exports = api;
    } else {
        root.KdlCsv = api;
    }
})(this);
