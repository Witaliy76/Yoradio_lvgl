# Factory text font source

`yoradio_factory_font.ttf` is the production raw factory text face. It is
embedded directly in firmware Flash and opened by `FontProvider` through
LVGL TinyTTF; it is not copied to SRAM and it is not a generated per-size font.

The one authoritative build selection is the `custom_text_ttf_source` setting
in `[env:4848S040]` in `platformio.ini`.

To test or adopt another factory text face:

1. put the raw `.ttf` in this directory;
2. change only `custom_text_ttf_source`;
3. rebuild `4848S040`.

`font_source_build.py` generates the `.incbin` assembly intermediate under
`.pio`; no generated font artifact belongs in source control. One scalable TTF
serves every runtime pixel-size request made through `FontProvider::text(px)`.

Accepted Slice 7 factory identity:

- bytes: `28000`
- SHA256: `4474F1CBC068BBBA4496ACEF6142C9F8F7AD9741B66F7BBB3E74BE83E8EE62A8`
- provenance: 457 requested Unicode values, 422 actual cmap mappings,
  35 requested values absent from the source.
