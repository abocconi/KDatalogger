/*
 * Time shift tests: node tools/viewer/test/time_shift.test.js
 * Run by tools/gen_viewer.py before building the page.
 */
'use strict';

const assert = require('assert');
const KdlShift = require('../src/time_shift.js');

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

test('snap: whole sample periods, millisecond rounding, no -0', () => {
    assert.strictEqual(KdlShift.snap(1.26, 0.1), 1.3);
    assert.strictEqual(KdlShift.snap(-0.74, 0.25), -0.75);
    assert.strictEqual(KdlShift.snap(3.4, 1), 3);
    /* median step as the parser computes it: 0.3 - 0.2 */
    assert.strictEqual(KdlShift.snap(0.7, 0.3 - 0.2), 0.7);
    assert.strictEqual(KdlShift.snap(0.3, 0.1), 0.3);
    assert.ok(Object.is(KdlShift.snap(-0.04, 0.1), 0));
    assert.strictEqual(KdlShift.snap(0.12345, 0), 0.123);
    assert.strictEqual(KdlShift.snap(0.12345, null), 0.123);
});

test('shiftTimes: shifted samples coincide with another file\'s', () => {
    const time = [0, 0.1, 0.2, 0.3];
    assert.strictEqual(KdlShift.shiftTimes(time, 0), time);
    const moved = KdlShift.shiftTimes(time, KdlShift.snap(0.2, 0.1));
    assert.deepStrictEqual(moved, [0.2, 0.3, 0.4, 0.5]);
    /* 0.1 + 0.2 is 0.30000000000000004 in float: must land on 0.3 */
    assert.strictEqual(KdlShift.shiftTimes([0.1], 0.2)[0], 0.3);
    assert.deepStrictEqual(KdlShift.shiftTimes([1, 2], -1.5), [-0.5, 0.5]);
    assert.deepStrictEqual(time, [0, 0.1, 0.2, 0.3], 'input untouched');
});

test('parseOffset: Italian and English decimals, sign, unit', () => {
    assert.strictEqual(KdlShift.parseOffset('+1,5'), 1.5);
    assert.strictEqual(KdlShift.parseOffset('-0.25'), -0.25);
    assert.strictEqual(KdlShift.parseOffset('−2 s'), -2);
    assert.strictEqual(KdlShift.parseOffset(' 3 '), 3);
    assert.strictEqual(KdlShift.parseOffset(',5'), 0.5);
    assert.strictEqual(KdlShift.parseOffset('0'), 0);
    assert.strictEqual(KdlShift.parseOffset(''), null);
    assert.strictEqual(KdlShift.parseOffset('abc'), null);
    assert.strictEqual(KdlShift.parseOffset('1,5,2'), null);
    assert.strictEqual(KdlShift.parseOffset('1e3'), null);
    assert.strictEqual(KdlShift.parseOffset('100000'), null);
});

if (failures > 0) {
    console.log(failures + ' test(s) failed');
    process.exit(1);
}
