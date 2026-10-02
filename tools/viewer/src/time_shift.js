/*
 * KDatalogger time shift: moves a log along the time axis in compare mode, so
 * similar events in different runs line up. Offsets are whole sample periods
 * and times are rounded to the millisecond, so files logged at the same rate
 * keep sharing their x values once joined (no float near-duplicates).
 *
 * Shared by the page (window.KdlShift) and the node test (module.exports).
 */
(function (root) {
    'use strict';

    var MAX_OFFSET_S = 86400;      /* a day: past this a typed value is a typo */

    /** Seconds rounded to the millisecond; never -0. */
    function toMs(seconds) {
        return Math.round(seconds * 1000) / 1000 || 0;
    }

    /** Offset rounded to a whole number of steps (the file's sample period). */
    function snap(offset, step) {
        var s = toMs(step);
        if (!(s > 0)) {
            s = 0.001;
        }
        return toMs(Math.round(offset / s) * s);
    }

    /** Sample times moved by offset; the same array when there is nothing to move. */
    function shiftTimes(time, offset) {
        if (!offset) {
            return time;
        }
        return time.map(function (t) { return toMs(t + offset); });
    }

    /** "+1,5", "-0.25", "−2 s" -> seconds; null if not a number or out of range. */
    function parseOffset(text) {
        var s = String(text).trim().replace(/\s*s$/i, '').replace(/^\+/, '')
            .replace('−', '-').replace(',', '.');
        if (!/^-?(\d+\.?\d*|\.\d+)$/.test(s)) {
            return null;
        }
        var v = Number(s);
        return Math.abs(v) <= MAX_OFFSET_S ? v : null;
    }

    var api = { snap: snap, shiftTimes: shiftTimes, parseOffset: parseOffset };

    if (typeof module !== 'undefined' && module.exports) {
        module.exports = api;
    } else {
        root.KdlShift = api;
    }
})(this);
