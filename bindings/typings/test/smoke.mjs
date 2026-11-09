import assert from 'node:assert/strict';
import { ENUMS, PROPS, enumName, enumValue, resolvePath } from '../dist/index.js';

assert.deepEqual(resolvePath('massif::TileLayer', 'visible'), { code: 'b' });
assert.equal(resolvePath('massif::TileLayer', 'noSuchProperty'), null);

for (const [name, constants] of Object.entries(ENUMS)) {
    for (const entry of constants.split(',')) {
        const [constant, value] = entry.split('=');
        assert.equal(typeof enumValue(constant), 'number', `${name}.${constant}`);
        assert.equal(enumName(name, Number(value)), constant, `${name}.${constant}`);
    }
}
assert.ok(Object.keys(PROPS).length > 100);
console.log('typings smoke test passed');
