/*
 * Parser tests: node tools/viewer/test/csv_parser.test.js
 * Run by tools/gen_viewer.py before building the page.
 */
'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const KdlCsv = require('../src/csv_parser.js');

let failures = 0;
function test(name, fn) {
    try {
        fn();
        console.log('ok   ' + name);
    } catch (err) {
        failures++;
        console.log('FAIL ' + name + '\n     ' + err.message);
    }
}

const BOM = '﻿';
const FIRMWARE_CSV = BOM +
    'Data;Ora;Tempo [s];Cil 1 [°C];Acqua [°C];P IC in [V]\r\n' +
    '26/09/2026;14:32:05;0,00;241,25;;0,532\r\n' +
    '26/09/2026;14:32:05;0,10;242,50;;0,540\r\n' +
    '26/09/2026;14:32:05;0,22;;;0,551\r\n';

test('firmware format: BOM, CRLF, decimal comma, units', () => {
    const d = KdlCsv.parse(FIRMWARE_CSV, 'log_0001.csv');
    assert.deepStrictEqual(d.time, [0, 0.1, 0.22]);
    assert.strictEqual(d.columns.length, 3);
    assert.deepStrictEqual(d.columns.map((c) => c.name), ['Cil 1', 'Acqua', 'P IC in']);
    assert.deepStrictEqual(d.columns.map((c) => c.unit), ['°C', '°C', 'V']);
    assert.strictEqual(d.columns[0].label, 'Cil 1 [°C]');
    assert.strictEqual(d.startDate, '26/09/2026');
    assert.strictEqual(d.startTime, '14:32:05');
    assert.strictEqual(d.rowCount, 3);
    assert.deepStrictEqual(d.warnings, []);
});

test('empty cell is null (a gap), not zero', () => {
    const d = KdlCsv.parse(FIRMWARE_CSV, 'x.csv');
    assert.deepStrictEqual(d.columns[0].values, [241.25, 242.5, null]);
    assert.deepStrictEqual(d.columns[1].values, [null, null, null]);
    assert.strictEqual(d.columns[0].validCount, 2);
    assert.strictEqual(d.columns[1].validCount, 0);
});

test('clock not set: empty Data/Ora', () => {
    const d = KdlCsv.parse('Data;Ora;Tempo [s];Olio [°C]\n;;0,00;86,00\n;;1,00;86,25\n', 'x.csv');
    assert.strictEqual(d.startDate, '');
    assert.strictEqual(d.startTime, '');
    assert.strictEqual(d.periodS, 1);
});

test('Excel re-save: comma separator and dot decimals', () => {
    const d = KdlCsv.parse('Data,Ora,Tempo [s],Cil 1 [°C]\n26/09/2026,14:00:00,0.5,300.25\n', 'x.csv');
    assert.deepStrictEqual(d.time, [0.5]);
    assert.deepStrictEqual(d.columns[0].values, [300.25]);
});

test('thousands separator with decimal comma', () => {
    assert.strictEqual(KdlCsv.parseNumber('1.234,5', true), 1234.5);
    assert.strictEqual(KdlCsv.parseNumber('12.5', true), 12.5);
    assert.strictEqual(KdlCsv.parseNumber(' ', true), null);
    assert.strictEqual(KdlCsv.parseNumber('abc', true), null);
});

test('quoted cells', () => {
    assert.deepStrictEqual(KdlCsv.splitLine('"a;b";"c""d";e', ';'), ['a;b', 'c"d', 'e']);
});

test('header without unit', () => {
    assert.deepStrictEqual(KdlCsv.parseHeaderCell(' Giri '), { name: 'Giri', unit: '' });
});

test('rows without a time are skipped with a warning', () => {
    const d = KdlCsv.parse('Tempo [s];A [V]\n0,0;1\nxx;2\n0,2;3\n', 'x.csv');
    assert.deepStrictEqual(d.time, [0, 0.2]);
    assert.strictEqual(d.warnings.length, 1);
});

test('unsorted rows are reordered', () => {
    const d = KdlCsv.parse('Tempo [s];A [V]\n0,2;3\n0,0;1\n0,1;2\n', 'x.csv');
    assert.deepStrictEqual(d.time, [0, 0.1, 0.2]);
    assert.deepStrictEqual(d.columns[0].values, [1, 2, 3]);
});

test('not a datalogger log: missing Tempo column', () => {
    assert.throws(() => KdlCsv.parse('a;b\n1;2\n', 'x.csv'), /Tempo/);
});

test('header only is rejected', () => {
    assert.throws(() => KdlCsv.parse(BOM + 'Data;Ora;Tempo [s];A [V]\r\n', 'x.csv'), /vuoto/);
});

test('generated sample files parse', () => {
    const dir = path.join(__dirname, '..', 'samples');
    const files = fs.existsSync(dir) ? fs.readdirSync(dir).filter((f) => f.endsWith('.csv')) : [];
    assert.ok(files.length > 0, 'no samples: run tools/viewer/gen_sample_csv.py');
    files.forEach((f) => {
        const d = KdlCsv.parse(fs.readFileSync(path.join(dir, f), 'utf8'), f);
        assert.strictEqual(d.columns.length, 14, f + ': expected 8 TC + 5 pressure + rpm columns');
        assert.deepStrictEqual(d.columns.slice(8).map((c) => c.unit),
                               ['bar', 'bar', 'bar', 'bar', 'bar', 'rpm'], f + ': units');
        assert.ok(d.rowCount > 100, f + ': too few rows');
    });
});

if (failures > 0) {
    console.log(failures + ' test(s) failed');
    process.exit(1);
}
