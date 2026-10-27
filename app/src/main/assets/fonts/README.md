# Fonts used by the in-game menu

The overlay menu draws its text with fonts that ship in the app, so it looks the same on every phone and never
depends on what the phone's maker installed (the system Chinese font on recent Android cannot be read by the
font engine the menu uses, which used to leave the menu blank).

| File | What it is | Licence |
| --- | --- | --- |
| `Geist-Regular.ttf` | Geist by Vercel: Latin and Cyrillic text | SIL OFL 1.1 (`Geist-OFL.txt`) |
| `NotoSansSC-Subset.ttf` | Noto Sans SC reduced to the characters the Chinese texts use | SIL OFL 1.1 (`NotoSansSC-OFL.txt`) |

## Adding or changing Chinese text

The Chinese font only has the characters that appear in `app/src/main/cpp/utils/i18n.h`. After changing those texts,
rebuild it from the full Noto Sans SC variable font (`NotoSansSC-VF.ttf` from https://github.com/notofonts/noto-cjk):

```bash
python -m fontTools.varLib.instancer NotoSansSC-VF.ttf wght=400 -o NotoSansSC-Regular-static.ttf
python -m fontTools.subset NotoSansSC-Regular-static.ttf --text-file=chars.txt \
  --output-file=NotoSansSC-Subset.ttf --layout-features= --no-hinting
```

where `chars.txt` holds every character that is not plain ASCII in `i18n.h`.
