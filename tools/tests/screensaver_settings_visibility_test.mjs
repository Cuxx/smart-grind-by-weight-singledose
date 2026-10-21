// Verify that screensaver-specific WebUI controls follow the selected design.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const page = readFileSync(new URL('../../src/network/device_page.h', import.meta.url), 'utf8');
const script = page.split('<script>')[1].split('</script>')[0];
assert.match(page, /\[hidden\]\{display:none!important\}/,
  'the field grid CSS must not override visibility state');
const start = script.indexOf('function setCheck(');
const end = script.indexOf('function clockText()', start);
assert.ok(start >= 0 && end > start, 'screensaver visibility helpers must be present');

const fields = new Map();
const controls = new Map();
for (const id of ['clockLayout', 'clockPosition', 'clockFont', 'clockColor', 'timezone',
  'gaggimateHost', 'purgeAmount', 'freshness']) {
  const field = {hidden: false};
  fields.set(id, field);
  controls.set(id, {closest: selector => selector === '.field' ? field : null});
}
for (const id of ['chooseClockFont', 'deleteClockFont']) controls.set(id, {hidden: false});
controls.set('purgeMode', {value: '0'});

const cards = ['minimal', 'clock', 'clock_custom', 'gaggimate'].map(style => ({
  dataset: {style},
  classList: {toggle() {}},
}));
const context = vm.createContext({
  state: {screensaverStyle: 'minimal'},
  $: id => controls.get(id),
  document: {querySelectorAll: selector => selector === '.stylecard' ? cards : []},
});
vm.runInContext(script.slice(start, end), context);

const assertVisibility = (style, clockVisible, gaggimateVisible) => {
  context.selectStyle(style);
  for (const id of ['clockLayout', 'clockPosition', 'clockFont', 'clockColor', 'timezone']) {
    assert.equal(fields.get(id).hidden, !clockVisible, `${id} visibility for ${style}`);
  }
  assert.equal(controls.get('chooseClockFont').hidden, !clockVisible, `font upload visibility for ${style}`);
  assert.equal(controls.get('deleteClockFont').hidden, !clockVisible, `font removal visibility for ${style}`);
  assert.equal(fields.get('gaggimateHost').hidden, !gaggimateVisible, `GaggiMate visibility for ${style}`);
};

assertVisibility('minimal', false, false);
assertVisibility('clock', true, false);
assertVisibility('clock_custom', true, false);
assertVisibility('gaggimate', false, true);
for (const [mode, purgeVisible] of [['0', false], ['1', true]]) {
  controls.get('purgeMode').value = mode;
  context.updatePurgeModeSettings();
  assert.equal(fields.get('purgeAmount').hidden, !purgeVisible, `purge amount visibility for mode ${mode}`);
  assert.equal(fields.get('freshness').hidden, !purgeVisible, `freshness visibility for mode ${mode}`);
}
console.log('Screensaver settings visibility tests passed.');
