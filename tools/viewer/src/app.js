/*
 * KDatalogger log viewer: loads the CSV files chosen or dropped by the user,
 * draws one chart per unit (one y axis per chart, cursors and zoom linked),
 * or compares one channel across files aligned on t = 0.
 */
(function () {
    'use strict';

    var SERIES_SLOTS = 8;          /* palette size: never generate a 9th hue */
    var PANEL_HEIGHT = 300;
    var COMPARE_HEIGHT = 380;
    var MINUTES_FROM_S = 120;      /* time axis switches to m:ss past this span */
    /* Tick steps in seconds for an m:ss axis: whole clock units, never 50 s. */
    var MINUTE_INCRS = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400];
    var UNIT_TITLES = { '°C': 'Temperature', 'V': 'Tensioni', 'bar': 'Pressioni', 'mV': 'Tensioni' };
    var UNIT_DECIMALS = { '°C': 1, 'V': 3, 'mV': 0, 'bar': 2 };

    var state = {
        files: [],             /* { id, key, name, data } in load order */
        nextId: 1,
        mode: 'single',
        fileId: null,          /* file shown in single mode */
        channel: null,         /* channel label compared in compare mode */
        hiddenChannels: {},    /* label -> true, single mode */
        hiddenFiles: {},       /* file id -> true, compare mode */
    };

    /** Live charts: { u, panel, rows, series, decimals, minutes }. */
    var plots = [];
    var plotByU = typeof Map !== 'undefined' ? new Map() : null;
    var syncingScale = false;

    var $ = function (id) { return document.getElementById(id); };

    /* ---------------------------------------------------------------- format */

    var numberFormats = {};
    function formatNumber(value, decimals) {
        if (value === null || value === undefined || !isFinite(value)) {
            return '—';
        }
        var fmt = numberFormats[decimals];
        if (!fmt) {
            fmt = new Intl.NumberFormat('it-IT', {
                minimumFractionDigits: decimals,
                maximumFractionDigits: decimals,
                useGrouping: false,
            });
            numberFormats[decimals] = fmt;
        }
        return fmt.format(value);
    }

    /** Fewest decimals that show a tick step exactly (0,25 needs 2, not 1). */
    function decimalsForStep(step) {
        if (!(step > 0)) {
            return 0;
        }
        var d = 0;
        while (d < 3 && Math.abs(step * Math.pow(10, d) - Math.round(step * Math.pow(10, d))) > 1e-6) {
            d++;
        }
        return d;
    }

    /** Time in seconds as "12,5 s" or, for long spans, "3:07,5". */
    function formatTime(seconds, minutes, decimals) {
        if (seconds === null || seconds === undefined || !isFinite(seconds)) {
            return '—';
        }
        if (!minutes) {
            return formatNumber(seconds, decimals);
        }
        var sign = seconds < 0 ? '-' : '';
        var abs = Math.abs(seconds);
        var m = Math.floor(abs / 60);
        var s = abs - m * 60;
        var sText = formatNumber(s, decimals);
        if (s < 10) {
            sText = '0' + sText;
        }
        return sign + m + ':' + sText;
    }

    function formatDuration(seconds) {
        return seconds >= MINUTES_FROM_S ? formatTime(seconds, true, 0) + ' min'
                                         : formatNumber(seconds, 1) + ' s';
    }

    function fileMeta(data) {
        var parts = [];
        if (data.startDate || data.startTime) {
            parts.push((data.startDate + ' ' + data.startTime).trim());
        }
        parts.push(formatDuration(data.durationS));
        var samples = data.rowCount + ' campioni';
        if (data.periodS) {
            samples += ' ogni ' + formatNumber(data.periodS, data.periodS < 1 ? 2 : 1) + ' s';
        }
        parts.push(samples);
        return parts.join(' · ');
    }

    function unitTitle(unit) {
        return UNIT_TITLES[unit] || (unit ? 'Grandezze in ' + unit : 'Senza unità');
    }

    function unitDecimals(unit) {
        return unit in UNIT_DECIMALS ? UNIT_DECIMALS[unit] : 2;
    }

    /* ----------------------------------------------------------------- theme */

    function cssVar(name) {
        return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
    }

    function theme() {
        var series = [];
        for (var i = 1; i <= SERIES_SLOTS; i++) {
            series.push(cssVar('--series-' + i));
        }
        return {
            surface: cssVar('--surface'),
            ink: cssVar('--ink'),
            ink2: cssVar('--ink-2'),
            muted: cssVar('--muted'),
            grid: cssVar('--grid'),
            axis: cssVar('--axis'),
            series: series,
        };
    }

    /* -------------------------------------------------------------- messages */

    function showMessage(text, isError) {
        var shown = $('messages').querySelectorAll('.msg-text');
        for (var i = 0; i < shown.length; i++) {
            if (shown[i].textContent === text) {
                return;
            }
        }
        var box = document.createElement('div');
        box.className = 'msg' + (isError ? ' error' : '');
        var span = document.createElement('span');
        span.className = 'msg-text';
        span.textContent = text;
        var close = document.createElement('button');
        close.type = 'button';
        close.setAttribute('aria-label', 'Chiudi');
        close.textContent = '×';
        close.addEventListener('click', function () { box.remove(); });
        box.appendChild(span);
        box.appendChild(close);
        $('messages').appendChild(box);
    }

    /* ----------------------------------------------------------- file input */

    function readText(file) {
        return new Promise(function (resolve, reject) {
            var reader = new FileReader();
            reader.onload = function () {
                var buffer = reader.result;
                /* The logger writes UTF-8; a file re-saved by Excel may be
                 * Windows-1252, which strict UTF-8 decoding rejects. */
                try {
                    resolve(new TextDecoder('utf-8', { fatal: true }).decode(buffer));
                } catch (e) {
                    resolve(new TextDecoder('windows-1252').decode(buffer));
                }
            };
            reader.onerror = function () { reject(reader.error); };
            reader.readAsArrayBuffer(file);
        });
    }

    function addFiles(fileList) {
        var files = Array.prototype.slice.call(fileList || []);
        if (files.length === 0) {
            return;
        }
        files.sort(function (a, b) { return a.name.localeCompare(b.name, 'it', { numeric: true }); });

        var jobs = files.map(function (file) {
            var key = file.name + '|' + file.size + '|' + file.lastModified;
            var already = state.files.some(function (f) { return f.key === key; });
            if (already) {
                return Promise.resolve(null);
            }
            return readText(file).then(function (text) {
                var data = KdlCsv.parse(text, file.name);
                data.warnings.forEach(function (w) { showMessage(file.name + ': ' + w, false); });
                return { id: state.nextId++, key: key, name: file.name, data: data };
            }).catch(function (err) {
                showMessage(file.name + ': ' + (err && err.message ? err.message : err), true);
                return null;
            });
        });

        Promise.all(jobs).then(function (loaded) {
            var added = loaded.filter(Boolean);
            if (added.length === 0) {
                return;
            }
            state.files = state.files.concat(added);
            state.fileId = added[0].id;
            if (!state.channel) {
                state.channel = defaultChannel();
            }
            render();
        });
    }

    function removeFile(id) {
        state.files = state.files.filter(function (f) { return f.id !== id; });
        delete state.hiddenFiles[id];
        if (state.fileId === id) {
            state.fileId = state.files.length ? state.files[0].id : null;
        }
        if (state.files.length < 2) {
            state.mode = 'single';
        }
        if (state.channel && channelLabels().indexOf(state.channel) < 0) {
            state.channel = defaultChannel();
        }
        render();
    }

    function currentFile() {
        for (var i = 0; i < state.files.length; i++) {
            if (state.files[i].id === state.fileId) {
                return state.files[i];
            }
        }
        return state.files[0] || null;
    }

    /** Every channel label across the loaded files, in first-seen order. */
    function channelLabels() {
        var seen = {};
        var labels = [];
        state.files.forEach(function (f) {
            f.data.columns.forEach(function (col) {
                if (!seen[col.label]) {
                    seen[col.label] = true;
                    labels.push(col.label);
                }
            });
        });
        return labels;
    }

    function defaultChannel() {
        var labels = channelLabels();
        for (var i = 0; i < labels.length; i++) {
            var withData = state.files.some(function (f) {
                return f.data.columns.some(function (c) {
                    return c.label === labels[i] && c.validCount > 0;
                });
            });
            if (withData) {
                return labels[i];
            }
        }
        return labels[0] || null;
    }

    /* ------------------------------------------------------------- controls */

    function fillSelect(select, options, selected) {
        select.textContent = '';
        options.forEach(function (opt) {
            var o = document.createElement('option');
            o.value = String(opt.value);
            o.textContent = opt.text;
            if (String(opt.value) === String(selected)) {
                o.selected = true;
            }
            select.appendChild(o);
        });
    }

    function renderControls() {
        var hasFiles = state.files.length > 0;
        $('controls').hidden = !hasFiles;
        $('empty').hidden = hasFiles;
        $('btn-reset').disabled = !hasFiles;
        $('btn-png').disabled = !hasFiles;
        if (!hasFiles) {
            return;
        }

        var compare = state.mode === 'compare';
        $('mode-single').setAttribute('aria-pressed', String(!compare));
        $('mode-compare').setAttribute('aria-pressed', String(compare));
        $('mode-compare').disabled = state.files.length < 2;
        $('mode-compare').title = state.files.length < 2 ? 'Apri almeno due file per confrontarli' : '';
        $('file-field').hidden = compare;
        $('channel-field').hidden = !compare;

        fillSelect($('sel-file'), state.files.map(function (f) {
            return { value: f.id, text: f.name };
        }), state.fileId);
        fillSelect($('sel-channel'), channelLabels().map(function (label) {
            return { value: label, text: label };
        }), state.channel);
    }

    function renderFiles() {
        var box = $('files');
        box.textContent = '';
        var compare = state.mode === 'compare';
        var colors = theme().series;

        state.files.forEach(function (f, index) {
            var chip = document.createElement('div');
            chip.className = 'chip' + (!compare && f.id === state.fileId ? ' selected' : '');

            var main = document.createElement('button');
            main.type = 'button';
            main.className = 'chip-main';
            main.title = compare ? '' : 'Mostra questo file';
            if (compare) {
                var key = document.createElement('span');
                key.className = 'line-key';
                key.style.borderTopColor = index < SERIES_SLOTS ? colors[index] : 'transparent';
                main.appendChild(key);
            }
            var text = document.createElement('span');
            text.className = 'chip-text';
            var name = document.createElement('span');
            name.className = 'chip-name';
            name.textContent = f.name;
            var meta = document.createElement('span');
            meta.className = 'chip-meta';
            meta.textContent = fileMeta(f.data);
            text.appendChild(name);
            text.appendChild(meta);
            main.appendChild(text);
            main.addEventListener('click', function () {
                if (state.mode === 'single') {
                    state.fileId = f.id;
                    render();
                }
            });

            var close = document.createElement('button');
            close.type = 'button';
            close.className = 'chip-close';
            close.setAttribute('aria-label', 'Chiudi ' + f.name);
            close.title = 'Chiudi il file';
            close.textContent = '×';
            close.addEventListener('click', function () { removeFile(f.id); });

            chip.appendChild(main);
            chip.appendChild(close);
            box.appendChild(chip);
        });
    }

    /* --------------------------------------------------------------- charts */

    function destroyPlots() {
        plots.forEach(function (p) { p.u.destroy(); });
        plots = [];
        if (plotByU) {
            plotByU.clear();
        }
        $('panels').textContent = '';
    }

    function entryFor(u) {
        if (plotByU) {
            return plotByU.get(u) || null;
        }
        for (var i = 0; i < plots.length; i++) {
            if (plots[i].u === u) {
                return plots[i];
            }
        }
        return null;
    }

    /** Nearest defined value around idx: compare mode aligns files with undefined gaps. */
    function valueNear(values, xs, idx) {
        var v = values[idx];
        if (v !== undefined) {
            return v;
        }
        var x = xs[idx];
        for (var d = 1; d < 400; d++) {
            var lo = idx - d;
            var hi = idx + d;
            var vLo = lo >= 0 ? values[lo] : undefined;
            var vHi = hi < values.length ? values[hi] : undefined;
            if (vLo !== undefined && vHi !== undefined) {
                return (x - xs[lo] <= xs[hi] - x) ? vLo : vHi;
            }
            if (vLo !== undefined) {
                return vLo;
            }
            if (vHi !== undefined) {
                return vHi;
            }
            if (lo < 0 && hi >= values.length) {
                break;
            }
        }
        return null;
    }

    function onCursor(u) {
        var entry = entryFor(u);
        if (!entry) {
            return;
        }
        var idx = u.cursor.idx;
        var xs = u.data[0];
        var hasIdx = idx !== null && idx !== undefined && idx >= 0 && idx < xs.length;
        entry.timeLabel.textContent = hasIdx
            ? 't = ' + formatTime(xs[idx], entry.minutes, entry.timeDecimals) + (entry.minutes ? '' : ' s')
            : '';
        entry.rows.forEach(function (row, i) {
            var value = hasIdx ? valueNear(u.data[i + 1], xs, idx) : null;
            row.cur.textContent = hasIdx ? formatNumber(value, entry.decimals) : '';
        });
    }

    function updateStats(entry) {
        var u = entry.u;
        var xs = u.data[0];
        var min = u.scales.x.min;
        var max = u.scales.x.max;
        var i0 = 0;
        var i1 = xs.length - 1;
        if (min !== null && min !== undefined) {
            while (i0 < xs.length && xs[i0] < min) { i0++; }
        }
        if (max !== null && max !== undefined) {
            while (i1 >= 0 && xs[i1] > max) { i1--; }
        }
        entry.rows.forEach(function (row, i) {
            var values = u.data[i + 1];
            var lo = Infinity;
            var hi = -Infinity;
            var sum = 0;
            var n = 0;
            for (var k = i0; k <= i1; k++) {
                var v = values[k];
                if (v === null || v === undefined) {
                    continue;
                }
                if (v < lo) { lo = v; }
                if (v > hi) { hi = v; }
                sum += v;
                n++;
            }
            row.min.textContent = n ? formatNumber(lo, entry.decimals) : '—';
            row.max.textContent = n ? formatNumber(hi, entry.decimals) : '—';
            row.avg.textContent = n ? formatNumber(sum / n, entry.decimals) : '—';
            row.stats = n ? { min: lo, max: hi, avg: sum / n } : null;
        });
    }

    function onScale(u, key) {
        if (key !== 'x') {
            return;
        }
        var entry = entryFor(u);
        if (!entry) {
            return;
        }
        updateStats(entry);
        if (syncingScale) {
            return;
        }
        syncingScale = true;
        var range = { min: u.scales.x.min, max: u.scales.x.max };
        plots.forEach(function (p) {
            if (p.u !== u) {
                p.u.setScale('x', range);
            }
        });
        syncingScale = false;
    }

    function axisFont(size) {
        return size + 'px system-ui, -apple-system, "Segoe UI", Roboto, sans-serif';
    }

    /**
     * One chart with its legend table.
     * spec: { title, subtitle, unit, xs, series: [{ label, color, values, hidden, empty,
     *         onToggle }], height }
     */
    function buildPanel(spec) {
        var t = theme();
        var panel = document.createElement('section');
        panel.className = 'panel';

        var head = document.createElement('div');
        head.className = 'panel-head';
        var h2 = document.createElement('h2');
        h2.textContent = spec.title;
        head.appendChild(h2);
        if (spec.subtitle) {
            var sub = document.createElement('span');
            sub.className = 'panel-sub';
            sub.textContent = spec.subtitle;
            head.appendChild(sub);
        }
        var timeLabel = document.createElement('span');
        timeLabel.className = 'cursor-time';
        head.appendChild(timeLabel);
        panel.appendChild(head);

        var chartBox = document.createElement('div');
        chartBox.className = 'chart';
        panel.appendChild(chartBox);

        var decimals = unitDecimals(spec.unit);
        var xs = spec.xs;
        var span = xs.length > 1 ? xs[xs.length - 1] - xs[0] : 0;
        var minutes = span >= MINUTES_FROM_S;

        /* Legend table: identity is never color-alone, and it is the table view. */
        var wrap = document.createElement('div');
        wrap.className = 'table-wrap';
        var table = document.createElement('table');
        table.className = 'legend';
        var thead = document.createElement('thead');
        var hr = document.createElement('tr');
        ['Canale', 'Al cursore', 'Min', 'Max', 'Media'].forEach(function (label, i) {
            var th = document.createElement('th');
            th.textContent = label;
            if (i === 2) {
                th.title = 'Nella zona visibile del grafico';
            }
            hr.appendChild(th);
        });
        thead.appendChild(hr);
        table.appendChild(thead);
        var tbody = document.createElement('tbody');
        table.appendChild(tbody);
        wrap.appendChild(table);
        panel.appendChild(wrap);
        $('panels').appendChild(panel);

        var entry = {
            u: null, panel: panel, rows: [], series: spec.series, decimals: decimals,
            minutes: minutes, timeDecimals: 1, timeLabel: timeLabel, title: spec.title,
            subtitle: spec.subtitle || '', unit: spec.unit,
        };

        var width = Math.max(280, chartBox.clientWidth || panel.clientWidth - 24);
        var opts = {
            width: width,
            height: spec.height || PANEL_HEIGHT,
            scales: { x: { time: false } },
            legend: { show: false },
            cursor: {
                sync: { key: 'kdl', setSeries: false },
                drag: { x: true, y: false, setScale: true },
                points: { size: 8, width: 2, fill: t.surface },
                focus: { prox: -1 },
            },
            series: [{ label: 'Tempo' }].concat(spec.series.map(function (s) {
                return {
                    label: s.label,
                    stroke: s.color,
                    width: 2,
                    show: !s.hidden,
                    spanGaps: false,
                    points: { show: false },
                };
            })),
            axes: [
                {
                    stroke: t.muted,
                    font: axisFont(12),
                    labelFont: axisFont(12),
                    label: minutes ? 'Tempo [min:s]' : 'Tempo [s]',
                    incrs: minutes ? MINUTE_INCRS : undefined,
                    labelSize: 22,
                    space: 70,
                    grid: { stroke: t.grid, width: 1 },
                    ticks: { stroke: t.axis, width: 1, size: 4 },
                    values: function (u, splits, axisIdx, space, incr) {
                        entry.timeDecimals = Math.max(1, decimalsForStep(incr));
                        var d = decimalsForStep(incr);
                        return splits.map(function (v) { return formatTime(v, minutes, d); });
                    },
                },
                {
                    stroke: t.muted,
                    font: axisFont(12),
                    size: 60,
                    grid: { stroke: t.grid, width: 1 },
                    ticks: { stroke: t.axis, width: 1, size: 4 },
                    values: function (u, splits, axisIdx, space, incr) {
                        var d = decimalsForStep(incr);
                        return splits.map(function (v) { return formatNumber(v, d); });
                    },
                },
            ],
            hooks: {
                setCursor: [onCursor],
                setScale: [onScale],
            },
        };

        var data = [xs].concat(spec.series.map(function (s) { return s.values; }));

        spec.series.forEach(function (s, i) {
            var tr = document.createElement('tr');
            if (s.hidden) {
                tr.className = 'off';
            }
            var tdName = document.createElement('td');
            var key = document.createElement('button');
            key.type = 'button';
            key.className = 'key';
            key.setAttribute('aria-pressed', String(!s.hidden));
            key.title = s.empty ? 'Nessun dato in questo file' : 'Mostra o nascondi';
            var line = document.createElement('span');
            line.className = 'line-key';
            line.style.borderTopColor = s.color;
            var label = document.createElement('span');
            label.textContent = s.label;
            key.appendChild(line);
            key.appendChild(label);
            if (s.empty) {
                key.disabled = true;
                var nodata = document.createElement('span');
                nodata.className = 'nodata';
                nodata.textContent = 'nessun dato';
                key.appendChild(nodata);
            }
            tdName.appendChild(key);
            tr.appendChild(tdName);

            var row = { tr: tr, key: key, stats: null };
            ['cur', 'min', 'max', 'avg'].forEach(function (name) {
                var td = document.createElement('td');
                td.className = name === 'cur' ? 'cur' : 'stat';
                tr.appendChild(td);
                row[name] = td;
            });
            key.addEventListener('click', function () {
                var show = !entry.u.series[i + 1].show;
                entry.u.setSeries(i + 1, { show: show });
                tr.className = show ? '' : 'off';
                key.setAttribute('aria-pressed', String(show));
                s.hidden = !show;
                if (s.onToggle) {
                    s.onToggle(show);
                }
            });
            tbody.appendChild(tr);
            entry.rows.push(row);
        });

        plots.push(entry);
        entry.u = new uPlot(opts, data, chartBox);
        if (plotByU) {
            plotByU.set(entry.u, entry);
        }
        updateStats(entry);
        onCursor(entry.u);
        return entry;
    }

    function renderSingle() {
        var file = currentFile();
        if (!file) {
            return;
        }
        var colors = theme().series;
        var groups = [];
        var byUnit = {};
        file.data.columns.forEach(function (col) {
            if (!(col.unit in byUnit)) {
                byUnit[col.unit] = { unit: col.unit, columns: [] };
                groups.push(byUnit[col.unit]);
            }
            byUnit[col.unit].columns.push(col);
        });

        groups.forEach(function (group) {
            var columns = group.columns;
            if (columns.length > SERIES_SLOTS) {
                showMessage(file.name + ': ' + columns.length + ' canali in ' + (group.unit || 'senza unità') +
                            ', mostrati i primi ' + SERIES_SLOTS, false);
                columns = columns.slice(0, SERIES_SLOTS);
            }
            buildPanel({
                title: unitTitle(group.unit),
                subtitle: group.unit ? '[' + group.unit + '] · ' + file.name : file.name,
                unit: group.unit,
                xs: file.data.time,
                series: columns.map(function (col, i) {
                    var empty = col.validCount === 0;
                    return {
                        label: col.name,
                        color: colors[i],
                        values: col.values,
                        empty: empty,
                        hidden: empty || !!state.hiddenChannels[col.label],
                        onToggle: function (show) {
                            if (show) {
                                delete state.hiddenChannels[col.label];
                            } else {
                                state.hiddenChannels[col.label] = true;
                            }
                        },
                    };
                }),
            });
        });
    }

    function renderCompare() {
        var colors = theme().series;
        var label = state.channel;
        var chosen = [];
        state.files.forEach(function (f, index) {
            if (index >= SERIES_SLOTS) {
                return;
            }
            var col = null;
            f.data.columns.forEach(function (c) {
                if (c.label === label) {
                    col = c;
                }
            });
            if (col) {
                chosen.push({ file: f, col: col, color: colors[index] });
            }
        });
        if (state.files.length > SERIES_SLOTS) {
            showMessage('Il confronto mostra al massimo ' + SERIES_SLOTS + ' file: chiudi quelli che non servono.',
                        false);
        }
        if (chosen.length === 0) {
            showMessage('Nessun file contiene il canale ' + label, false);
            return;
        }

        /* Align on t = 0 (every log starts there); undefined = no sample at
         * that x in this file, spanned; null = invalid reading, a real gap. */
        var tables = chosen.map(function (c) { return [c.file.data.time, c.col.values]; });
        var joined = uPlot.join(tables, tables.map(function () { return [2]; }));
        var unit = chosen[0].col.unit;

        buildPanel({
            title: chosen[0].col.name,
            subtitle: (unit ? '[' + unit + '] · ' : '') + 'confronto tra file, allineati sull\'inizio',
            unit: unit,
            xs: joined[0],
            height: COMPARE_HEIGHT,
            series: chosen.map(function (c, i) {
                var empty = c.col.validCount === 0;
                return {
                    label: c.file.name,
                    color: c.color,
                    values: joined[i + 1],
                    empty: empty,
                    hidden: empty || !!state.hiddenFiles[c.file.id],
                    onToggle: function (show) {
                        if (show) {
                            delete state.hiddenFiles[c.file.id];
                        } else {
                            state.hiddenFiles[c.file.id] = true;
                        }
                    },
                };
            }),
        });
    }

    function render() {
        destroyPlots();
        renderControls();
        renderFiles();
        if (state.files.length === 0) {
            return;
        }
        if (state.mode === 'compare') {
            renderCompare();
        } else {
            renderSingle();
        }
    }

    function resetZoom() {
        plots.forEach(function (p) {
            var xs = p.u.data[0];
            if (xs.length > 1) {
                p.u.setScale('x', { min: xs[0], max: xs[xs.length - 1] });
            }
        });
    }

    function resizePlots() {
        plots.forEach(function (p) {
            var box = p.u.root.parentNode;
            var width = Math.max(280, box.clientWidth);
            if (Math.abs(width - p.u.width) > 1) {
                p.u.setSize({ width: width, height: p.u.height });
            }
        });
    }

    /* ----------------------------------------------------------- PNG export */

    var BLOCK_GAP = 24;            /* px between charts in the exported image */

    function exportPng() {
        if (plots.length === 0) {
            return;
        }
        var t = theme();
        var pr = window.devicePixelRatio || 1;
        var pad = 16 * pr;
        var font = function (size, weight) {
            return (weight || 400) + ' ' + Math.round(size * pr) + 'px system-ui, -apple-system, "Segoe UI", sans-serif';
        };
        var probe = document.createElement('canvas').getContext('2d');

        var width = 0;
        plots.forEach(function (p) { width = Math.max(width, p.u.ctx.canvas.width); });
        width += 2 * pad;

        /* Lay out each block first to size the canvas. */
        var lineH = 20 * pr;
        var blocks = plots.map(function (p) {
            var items = [];
            p.series.forEach(function (s, i) {
                if (s.hidden) {
                    return;
                }
                var st = p.rows[i].stats;
                var text = s.label + (st ? '  max ' + formatNumber(st.max, p.decimals) +
                                             '  min ' + formatNumber(st.min, p.decimals) : '');
                items.push({ color: s.color, text: text });
            });
            probe.font = font(12);
            var rows = [[]];
            var x = 0;
            items.forEach(function (item) {
                var w = 22 * pr + probe.measureText(item.text).width + 18 * pr;
                if (x + w > width - 2 * pad && rows[rows.length - 1].length) {
                    rows.push([]);
                    x = 0;
                }
                rows[rows.length - 1].push(item);
                x += w;
            });
            var canvas = p.u.ctx.canvas;
            return {
                p: p, canvas: canvas, rows: rows,
                height: 28 * pr + canvas.height + 6 * pr + rows.length * lineH + BLOCK_GAP * pr,
            };
        });

        var headerH = 8 * pr;
        var totalH = pad + headerH + blocks.reduce(function (acc, b) { return acc + b.height; }, 0) + pad;
        var out = document.createElement('canvas');
        out.width = Math.ceil(width);
        out.height = Math.ceil(totalH);
        var ctx = out.getContext('2d');
        ctx.fillStyle = t.surface;
        ctx.fillRect(0, 0, out.width, out.height);
        ctx.textBaseline = 'middle';

        var y = pad + headerH;
        blocks.forEach(function (b) {
            ctx.fillStyle = t.ink;
            ctx.font = font(15, 600);
            ctx.fillText(b.p.title, pad, y + 10 * pr);
            var titleW = ctx.measureText(b.p.title).width;
            ctx.fillStyle = t.ink2;
            ctx.font = font(13);
            ctx.fillText(b.p.subtitle, pad + titleW + 10 * pr, y + 10 * pr);
            y += 28 * pr;

            ctx.drawImage(b.canvas, pad, y);
            y += b.canvas.height + 6 * pr;

            ctx.font = font(12);
            b.rows.forEach(function (row) {
                var x = pad;
                row.forEach(function (item) {
                    ctx.strokeStyle = item.color;
                    ctx.lineWidth = 2 * pr;
                    ctx.beginPath();
                    ctx.moveTo(x, y + lineH / 2);
                    ctx.lineTo(x + 16 * pr, y + lineH / 2);
                    ctx.stroke();
                    ctx.fillStyle = t.ink;
                    ctx.fillText(item.text, x + 22 * pr, y + lineH / 2);
                    x += 22 * pr + ctx.measureText(item.text).width + 18 * pr;
                });
                y += lineH;
            });
            y += BLOCK_GAP * pr;
        });

        var base = state.mode === 'compare'
            ? 'confronto_' + (state.channel || 'canale').replace(/\s*\[.*\]\s*$/, '')
            : (currentFile() ? currentFile().name.replace(/\.csv$/i, '') : 'grafico');
        var fileName = base.replace(/[^\w\-]+/g, '_') + '.png';
        download(out, fileName);
    }

    function download(canvas, fileName) {
        var save = function (href, revoke) {
            var a = document.createElement('a');
            a.href = href;
            a.download = fileName;
            document.body.appendChild(a);
            a.click();
            a.remove();
            if (revoke) {
                setTimeout(function () { URL.revokeObjectURL(href); }, 4000);
            }
        };
        if (canvas.toBlob) {
            canvas.toBlob(function (blob) { save(URL.createObjectURL(blob), true); }, 'image/png');
        } else {
            save(canvas.toDataURL('image/png'), false);
        }
    }

    /* ---------------------------------------------------------------- wiring */

    function init() {
        if (typeof uPlot === 'undefined' || typeof KdlCsv === 'undefined') {
            showMessage('Pagina incompleta: libreria grafica non caricata.', true);
            return;
        }

        ['file-input', 'file-input-empty'].forEach(function (id) {
            $(id).addEventListener('change', function (e) {
                addFiles(e.target.files);
                e.target.value = '';
            });
        });
        $('btn-reset').addEventListener('click', resetZoom);
        $('btn-png').addEventListener('click', exportPng);
        $('mode-single').addEventListener('click', function () {
            state.mode = 'single';
            render();
        });
        $('mode-compare').addEventListener('click', function () {
            if (state.files.length >= 2) {
                state.mode = 'compare';
                if (!state.channel) {
                    state.channel = defaultChannel();
                }
                render();
            }
        });
        $('sel-file').addEventListener('change', function (e) {
            state.fileId = Number(e.target.value);
            render();
        });
        $('sel-channel').addEventListener('change', function (e) {
            state.channel = e.target.value;
            render();
        });

        /* Drop anywhere on the page. */
        var depth = 0;
        document.addEventListener('dragenter', function (e) {
            if (e.dataTransfer && Array.prototype.indexOf.call(e.dataTransfer.types || [], 'Files') >= 0) {
                depth++;
                document.body.classList.add('dragging');
            }
        });
        document.addEventListener('dragleave', function () {
            depth = Math.max(0, depth - 1);
            if (depth === 0) {
                document.body.classList.remove('dragging');
            }
        });
        document.addEventListener('dragover', function (e) { e.preventDefault(); });
        document.addEventListener('drop', function (e) {
            e.preventDefault();
            depth = 0;
            document.body.classList.remove('dragging');
            if (e.dataTransfer && e.dataTransfer.files) {
                addFiles(e.dataTransfer.files);
            }
        });

        if (typeof ResizeObserver !== 'undefined') {
            new ResizeObserver(resizePlots).observe($('panels'));
        } else {
            window.addEventListener('resize', resizePlots);
        }

        /* Canvas colors are baked in at build time: rebuild on theme change. */
        var media = window.matchMedia ? window.matchMedia('(prefers-color-scheme: dark)') : null;
        if (media) {
            var onTheme = function () { render(); };
            if (media.addEventListener) {
                media.addEventListener('change', onTheme);
            } else if (media.addListener) {
                media.addListener(onTheme);
            }
        }

        renderControls();
    }

    init();
})();
