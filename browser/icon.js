#!/usr/bin/env node

"use strict";

const fs = require("node:fs");
const path = require("node:path");
const zlib = require("node:zlib");

function crc32(buffer) {
    let crc = 0xffffffff;
    for (const byte of buffer) {
        crc ^= byte;
        for (let bit = 0; bit < 8; ++bit) {
            crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
        }
    }
    return (crc ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
    const name = Buffer.from(type, "ascii");
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length);
    const checksum = Buffer.alloc(4);
    checksum.writeUInt32BE(crc32(Buffer.concat([name, data])));
    return Buffer.concat([length, name, data, checksum]);
}

function lineDistance(x, y, x1, y1, x2, y2) {
    const dx = x2 - x1;
    const dy = y2 - y1;
    const lengthSquared = dx * dx + dy * dy;
    const t = Math.max(0, Math.min(1, ((x - x1) * dx + (y - y1) * dy) / lengthSquared));
    return Math.hypot(x - (x1 + t * dx), y - (y1 + t * dy));
}

function insideRoundedRect(x, y) {
    const low = 0.125;
    const high = 0.875;
    const radius = 0.17;
    if ((x >= low + radius && x <= high - radius && y >= low && y <= high) ||
        (y >= low + radius && y <= high - radius && x >= low && x <= high)) {
        return true;
    }
    const cornerX = x < 0.5 ? low + radius : high - radius;
    const cornerY = y < 0.5 ? low + radius : high - radius;
    return Math.hypot(x - cornerX, y - cornerY) <= radius;
}

function coverage(size, pixelX, pixelY, predicate) {
    const samples = 4;
    let hits = 0;
    for (let y = 0; y < samples; ++y) {
        for (let x = 0; x < samples; ++x) {
            const sx = (pixelX + (x + 0.5) / samples) / size;
            const sy = (pixelY + (y + 0.5) / samples) / size;
            hits += predicate(sx, sy) ? 1 : 0;
        }
    }
    return hits / (samples * samples);
}

function blend(pixels, offset, color, alpha) {
    const destinationAlpha = pixels[offset + 3] / 255;
    const outputAlpha = alpha + destinationAlpha * (1 - alpha);
    if (outputAlpha === 0) {
        return;
    }
    for (let channel = 0; channel < 3; ++channel) {
        const destination = pixels[offset + channel] / 255;
        pixels[offset + channel] = Math.round(
            ((color[channel] / 255) * alpha + destination * destinationAlpha * (1 - alpha)) /
            outputAlpha * 255
        );
    }
    pixels[offset + 3] = Math.round(outputAlpha * 255);
}

function render(size) {
    const pixels = Buffer.alloc(size * size * 4);
    for (let y = 0; y < size; ++y) {
        for (let x = 0; x < size; ++x) {
            const offset = (y * size + x) * 4;
            blend(pixels, offset, [24, 32, 51], coverage(size, x, y, insideRoundedRect));
            const chevron = coverage(size, x, y, (sx, sy) =>
                Math.min(
                    lineDistance(sx, sy, 0.29, 0.34, 0.43, 0.50),
                    lineDistance(sx, sy, 0.43, 0.50, 0.29, 0.66)
                ) <= 0.038
            );
            blend(pixels, offset, [247, 249, 252], chevron);
            const underscore = coverage(size, x, y, (sx, sy) =>
                lineDistance(sx, sy, 0.52, 0.65, 0.72, 0.65) <= 0.038
            );
            blend(pixels, offset, [80, 200, 120], underscore);
        }
    }

    const scanlines = Buffer.alloc((size * 4 + 1) * size);
    for (let y = 0; y < size; ++y) {
        pixels.copy(scanlines, y * (size * 4 + 1) + 1, y * size * 4, (y + 1) * size * 4);
    }
    const header = Buffer.alloc(13);
    header.writeUInt32BE(size, 0);
    header.writeUInt32BE(size, 4);
    header[8] = 8;
    header[9] = 6;
    return Buffer.concat([
        Buffer.from("89504e470d0a1a0a", "hex"),
        chunk("IHDR", header),
        chunk("IDAT", zlib.deflateSync(scanlines, {level: 9})),
        chunk("IEND", Buffer.alloc(0))
    ]);
}

function main(arguments_) {
    const outputDirectory = arguments_[0];
    const requested = arguments_.slice(1).map(Number);
    const sizes = requested.length ? requested : [16, 32, 48, 128];
    if (!outputDirectory || sizes.some((size) => ![16, 32, 48, 128].includes(size))) {
        process.stderr.write("usage: icon.js OUTPUT_DIRECTORY [16 32 48 128]\n");
        process.exit(2);
    }

    fs.mkdirSync(outputDirectory, {recursive: true});
    for (const size of sizes) {
        const output = path.join(outputDirectory, `icon-${size}.png`);
        fs.writeFileSync(output, render(size));
        process.stdout.write(output + "\n");
    }
}

if (require.main === module) {
    main(process.argv.slice(2));
}

module.exports = {render};
