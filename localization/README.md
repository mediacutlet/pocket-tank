# Simplified Chinese translation resources

These UTF-8 catalogs collect translations developed while running Pocket Tank on a community ESP32-S3 port. The integrated Chinese UI, pixel-font renderer and board firmware are available in the [LCKFB SZPI community fork](https://github.com/ClingYang/pocket-tank-szpi-zh).

## Files

- `zh_CN.tsv` contains 695 English-to-Simplified-Chinese UI entries. Each line has an English source literal, one tab and its translation. It includes menu copy, onboarding, shop, milestones, updates and labels supplied by shared name tables.
- `zh_CN_home.tsv` contains the 64 home-water adjectives, 64 place names and the fallback name. Its first column identifies the context so a place name such as REEF can have its own translation. Chinese home-water names concatenate the adjective and place, using at most 15 UTF-8 bytes for the existing 16-byte display buffer.
- [中文上手指南](../docs/zh-CN.md) explains the project and everyday controls for Chinese-speaking readers.

## Translation details

Format conversions retain their original order and types, including `%s`, `%d`, floating-point precision and escaped percent signs. Empty translations for English articles and plural fragments join naturally into Chinese sentences. Leading and trailing spaces in source keys are significant, and empty second columns are intentional.

The Chinese UI implementation decodes UTF-8 code points, measures full-width glyph advances, and wraps text at code-point boundaries. Translated messages use byte capacities appropriate to UTF-8. The community implementation uses Fusion Pixel 8px and 12px bitmap fonts with their font licenses retained. The catalogs can be reused with another renderer and font.

Fish and keeper-supplied tank names are personal text. Model vocabulary, console commands, NVS keys and the radio protocol use their existing identifiers, while the presentation layer supplies translated labels.

## Provenance and checks

The source version is Pocket Tank v0.4.1, commit `952ef4d`. Translation and implementation used AI assistance, followed by source checks and real-device testing in the community port. All catalog printf conversions were checked for equality, source keys for duplicates, and all 4,096 home-name combinations for the 16-byte buffer boundary. Settings and shelf screenshots and measured runtime details are linked from the community fork.

This contribution provides the translation catalogs and Chinese documentation for review and reuse. The community fork contains the working firmware integration and its hardware-specific changes. Contributions follow the project license.
