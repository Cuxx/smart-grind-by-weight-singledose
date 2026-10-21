import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const read = path => readFileSync(new URL(path, import.meta.url), 'utf8');
const page = read('../../src/network/device_page.h');
const api = read('../../src/network/device_api.cpp');
const layout = read('../../src/ui/screens/grinding_screen_base.h');
const screen = read('../../src/ui/screens/grinding_screen.cpp');

assert.match(layout, /BEAN_FLOW = 4/);
assert.match(layout, /GRINDER_ROTOR = 5/);
assert.match(page, /<option value="4">Bean Flow<\/option>/);
assert.match(page, /<option value="5">Grinder Rotor<\/option>/);
assert.match(api, /settings\.grind_layout >= 0 && settings\.grind_layout <= 5/);
assert.match(screen, /GrindScreenLayout::BEAN_FLOW/);
assert.match(screen, /GrindScreenLayout::GRINDER_ROTOR/);

console.log('Grind layout option regression checks passed.');
