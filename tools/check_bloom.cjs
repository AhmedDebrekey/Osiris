const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const downShader = read('assets/shaders/bloom_downsample.frag');
const upShader = read('assets/shaders/bloom_upsample.frag');
const maxLevels = Number(read('engine/rhi_vulkan/VulkanRHI.h').match(/MAX_BLOOM_MIPS = (\d+)/)[1]);
assert.equal(maxLevels, 6);

// Read the filter topology and weights from the actual GLSL so edits cannot silently leave
// these checks testing an unrelated, hard-coded kernel. This is CPU math QA, not a GPU test.
const taps = new Map([...downShader.matchAll(/vec3 ([a-m]) = texture\(sourceColor, inUV(?: \+ texel \* vec2\(\s*([\d.-]+),\s*([\d.-]+)\))?\)\.rgb/g)]
    .map(m => [m[1], [Number(m[2] || 0), Number(m[3] || 0)]]));
const groups = [...downShader.matchAll(/AverageGroup\(([a-m]), ([a-m]), ([a-m]), ([a-m])\) \* ([\d.]+)/g)]
    .map(m => ({samples: m.slice(1, 5), weight: Number(m[5])}));
const tent = [...upShader.matchAll(/texture\(lowerMip, inUV(?: \+ stepUV \* vec2\(\s*([\d.-]+),\s*([\d.-]+)\))?\)\.rgb(?: \* ([\d.]+))?/g)]
    .map(m => ({x: Number(m[1] || 0), y: Number(m[2] || 0), weight: Number(m[3] || 1) / 16}));
assert.equal(taps.size, 13);
assert.equal(groups.length, 5);
assert.equal(tent.length, 9);
assert.equal(groups.reduce((sum, g) => sum + g.weight, 0), 1);
assert.equal(tent.reduce((sum, t) => sum + t.weight, 0), 1);

function dimensions(width, height) {
    const result = [];
    for (let i = 0; i < maxLevels; i++) {
        width = Math.max(1, Math.floor(width / 2));
        height = Math.max(1, Math.floor(height / 2));
        result.push([width, height]);
        if (width === 1 && height === 1) break;
    }
    return result;
}

function image(width, height, fn) {
    const data = new Float64Array(width * height);
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) data[y * width + x] = fn(x, y);
    return {width, height, data};
}

function sample(img, u, v) {
    const x = u * img.width - .5, y = v * img.height - .5;
    const x0 = Math.floor(x), y0 = Math.floor(y), fx = x - x0, fy = y - y0;
    const at = (xx, yy) => img.data[Math.min(img.height - 1, Math.max(0, yy)) * img.width
                                      + Math.min(img.width - 1, Math.max(0, xx))];
    return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy)
         + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

function groupAverage(values, first) {
    const weights = values.map(value => first ? 1 / (1 + value) : 1);
    return values.reduce((sum, value, i) => sum + value * weights[i], 0)
         / weights.reduce((a, b) => a + b, 0);
}

function downsample(src, width, height, first) {
    return image(width, height, (x, y) => {
        const u = (x + .5) / width, v = (y + .5) / height;
        const values = new Map([...taps].map(([name, [dx, dy]]) => [name,
            sample(src, u + dx / src.width, v + dy / src.height)]));
        return groups.reduce((sum, group) => sum + groupAverage(group.samples.map(s => values.get(s)), first) * group.weight, 0);
    });
}

function upsample(lower, detail, radius, detailWeight) {
    return image(detail.width, detail.height, (x, y) => {
        const u = (x + .5) / detail.width, v = (y + .5) / detail.height;
        const filtered = tent.reduce((sum, t) => sum + sample(lower,
            u + t.x * radius / lower.width, v + t.y * radius / lower.height) * t.weight, 0);
        return filtered * (1 - detailWeight) + detail.data[y * detail.width + x] * detailWeight;
    });
}

function pyramid(src, radius = 1) {
    const down = [];
    for (const [width, height] of dimensions(src.width, src.height)) {
        down.push(downsample(down.at(-1) || src, width, height, down.length === 0));
    }
    let result = down.at(-1);
    for (let i = down.length - 2; i >= 0; i--) result = upsample(result, down[i], radius, 1 / (down.length - i));
    return result;
}

for (const [width, height] of [[1920, 1080], [1501, 901], [7, 3], [1, 64], [64, 1], [1, 1]]) {
    const levels = dimensions(width, height);
    assert(levels.length >= 1 && levels.length <= 6);
    assert(levels.every(([w, h]) => w >= 1 && h >= 1));
    assert.equal(new Set(levels.map(d => d.join('x'))).size, levels.length);
}

for (const value of [0, .001, .4, .8, 1, 10, 64000]) {
    for (const [width, height] of [[31, 19], [1, 1], [1, 64]]) {
        const result = pyramid(image(width, height, () => value));
        assert(result.data.every(v => Number.isFinite(v) && Math.abs(v - value) < Math.max(1e-10, value * 1e-12)));
    }
}
assert(groupAverage([0, 0, 0, 10000], true) < 1);
assert.equal(groupAverage([0, 0, 0, 10000], false), 2500);

// Without first-pass firefly compression the normalized progressive reconstruction must be linear.
const detail = image(31, 19, (x, y) => .1 + x / 31 + y / 19);
const lower = image(15, 9, (x, y) => x * y / 100);
const combined = upsample(lower, detail, 1, .25);
const twice = upsample(image(15, 9, (x, y) => lower.data[y * 15 + x] * 2),
    image(31, 19, (x, y) => detail.data[y * 31 + x] * 2), 1, .25);
assert(twice.data.every((v, i) => Math.abs(v - combined.data[i] * 2) < 1e-12));

const impulse = image(128, 128, (x, y) => x >= 62 && x < 66 && y >= 62 && y < 66 ? 100 : 0);
const narrow = pyramid(impulse, .5), wide = pyramid(impulse, 2);
function variance(img) {
    let mass = 0, moment = 0;
    img.data.forEach((value, i) => {
        const dx = i % img.width - (img.width - 1) / 2;
        const dy = Math.floor(i / img.width) - (img.height - 1) / 2;
        mass += value;
        moment += value * (dx * dx + dy * dy);
    });
    return moment / mass;
}
assert(wide.data.every(v => Number.isFinite(v) && v >= 0));
assert(variance(wide) > variance(narrow));
assert(sample(wide, .25, .5) > 0);

const composite = read('assets/shaders/postprocess.frag');
assert(!composite.includes('bloomThreshold'));
assert(composite.indexOf('color += texture(bloomColor') < composite.indexOf('color = ToneMap(color)'));
for (const shader of ['triangle.frag', 'skybox.frag']) assert(!read('assets/shaders/' + shader).includes('2.51'));
assert.equal((composite.match(/float (?:vignetteIntensity|vignetteInnerRadius|vignetteOuterRadius|chromaticAberrationIntensity|filmGrainIntensity|bloomIntensity|bloomRadius);/g) || []).length, 7);
console.log('PASS: GLSL kernel weights, odd/tiny extents, constant HDR preservation, first-pass Karis, linear upsample, radius/spread, and HDR-before-tone-map composition.');
