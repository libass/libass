# Color Emoji Tests

This directory contains test cases for color emoji rendering support.

## Included Font

Tests use Noto Color Emoji (CBDT format), included in this directory:
- File: `NotoColorEmoji.ttf`
- License: OFL 1.1 (SIL Open Font License)
- Source: https://fonts.google.com/noto/specimen/Noto+Color+Emoji
- Native ppem: 109

## Test Cases

- `emoji_basic.ass` - Basic emoji rendering (grinning face, party popper, heart)
- `emoji_row.ass` - Row of various emoji types (face, sunglasses, party, rainbow, heart, fire, star, music)
- `emoji_mixed.ass` - Different emoji categories (hand, globe, sparkles, confetti, gift)
- `emoji_shadow.ass` - Emoji with shadow style settings

## Running Tests

From the libass root directory:

```bash
# Build compare tool (requires libpng)
./configure --enable-compare
make

# Run emoji tests
./compare/compare -i compare/test/emoji -p 0
```

The `-p 0` flag requires SAME (exact match). Use `-p 1` for GOOD (minor differences) or `-p 2` for BAD tolerance.

## Reference Images

Reference images are included:
- `emoji_basic-0000.png` - Reference for emoji_basic.ass at t=0
- `emoji_row-0000.png` - Reference for emoji_row.ass at t=0
- `emoji_mixed-0000.png` - Reference for emoji_mixed.ass at t=0
- `emoji_shadow-0000.png` - Reference for emoji_shadow.ass at t=0

## Font Compatibility

Color emoji support works with multiple font formats:
- **CBDT/CBLC** (Google/Noto): Bitmap-based, ppem varies by font
- **sbix** (Apple): Bitmap-based, typically ppem=160
- **COLR/CPAL**: Vector-based (planned support)
- **SVG**: Vector-based (planned support)

The rendering code auto-detects the font's native ppem from its fixed sizes table.
