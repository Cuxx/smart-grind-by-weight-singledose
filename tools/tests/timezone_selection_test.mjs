// Verify WebUI timezone choices map browser regions to firmware POSIX rules.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const page = readFileSync(new URL('../../src/network/device_page.h', import.meta.url), 'utf8');
const script = page.split('<script>')[1].split('</script>')[0];
const start = script.indexOf('const timezoneOptions=');
const end = script.indexOf("$('purgeMode').onchange", start);
assert.ok(start >= 0 && end > start, 'timezone mapping helpers must be present');

const controls = new Map([
  ['timezone', {value: 'CET-1CEST,M3.5.0,M10.5.0/3'}],
  ['timezoneSelect', {value: ''}],
  ['timezoneAdvanced', {open: false}],
]);
const context = vm.createContext({
  $: id => controls.get(id),
  toast() {},
  Intl: {
    DateTimeFormat: () => ({
      resolvedOptions: () => ({timeZone: 'Europe/Berlin'}),
    }),
  },
});
vm.runInContext(script.slice(start, end), context);
const evaluate = expression => vm.runInContext(expression, context);

assert.equal(evaluate("timezoneOptionForIana('Europe/Berlin')[2]"),
  'CET-1CEST,M3.5.0,M10.5.0/3');
assert.equal(evaluate("timezoneOptionForIana('America/Toronto')[2]"),
  'EST5EDT,M3.2.0,M11.1.0');
assert.equal(evaluate("timezoneOptionForIana('Asia/Kolkata')[2]"), 'IST-5:30');
assert.equal(evaluate("timezoneOptionForValue('JST-9')[0]"), 'Asia/Tokyo');
assert.equal(evaluate("timezoneOptionForIana('Pacific/Chatham')"), undefined);
assert.match(evaluate('browserFallbackTimezone()'), /^UTC[+-]\d{1,2}(?::\d{2})?$/);

evaluate('syncTimezoneSelection()');
assert.equal(controls.get('timezoneSelect').value, 'Europe/Berlin');
assert.equal(controls.get('timezoneAdvanced').open, false);

controls.get('timezone').value = 'CUSTOM-3';
evaluate('syncTimezoneSelection()');
assert.equal(controls.get('timezoneSelect').value, 'custom');
assert.equal(controls.get('timezoneAdvanced').open, true);

evaluate('applyBrowserTimezone()');
assert.equal(controls.get('timezone').value, 'CET-1CEST,M3.5.0,M10.5.0/3');
assert.equal(controls.get('timezoneSelect').value, 'Europe/Berlin');
assert.equal(controls.get('timezoneAdvanced').open, false);
console.log('Timezone selection mapping tests passed.');
