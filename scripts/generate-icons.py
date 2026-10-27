"""Generate original Neighborhood-inspired icons, at native Windows icon sizes."""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'src' / 'shell' / 'icons'
OUT.mkdir(exist_ok=True)
SIZES = [(n, n) for n in (16, 24, 32, 48, 64, 128, 256)]

def render(kind):
    im = Image.new('RGBA', (256, 256))
    d = ImageDraw.Draw(im)
    silver, light, edge, green = '#aeb6ba', '#f5f7f4', '#7b8889', '#a7d72f'
    if kind in ('root', 'add'):
        d.rounded_rectangle((30, 28, 222, 228), 15, fill='#647a55', outline=edge, width=5)
        d.rounded_rectangle((40, 37, 213, 216), 9, fill=green)
        rings = Image.new('RGBA', im.size)
        rd = ImageDraw.Draw(rings)
        for r in (38, 64, 90, 116, 142):
            rd.arc((126-r, 65-r, 126+r, 65+r), 0, 180, fill='#e0f3a1', width=7)
        im.alpha_composite(rings.crop((43,40,210,212)), (43,40))
        d.ellipse((112, 49, 140, 77), fill=light)
        if kind == 'add':
            d.rectangle((179, 2, 203, 90), fill='#71ba16')
            d.rectangle((147, 34, 235, 58), fill='#71ba16')
    elif kind in ('console', 'default'):
        d.polygon([(77, 24), (155, 12), (190, 30), (184, 232), (145, 245), (72, 220), (83, 126)], fill=silver, outline=edge, width=4)
        d.polygon([(82, 27), (154, 17), (146, 237), (78, 217), (89, 126)], fill=light)
        d.polygon([(159, 21), (181, 33), (177, 226), (153, 236)], fill='#c5cecc')
        d.rounded_rectangle((103, 46, 130, 102), 3, fill='#c0c8c4', outline=edge, width=2)
        d.line((106, 71, 129, 70), fill=edge, width=3)
        d.ellipse((99, 147, 126, 174), fill='#56764e')
        d.ellipse((104, 152, 121, 169), fill='#82bd30')
        d.rounded_rectangle((99, 193, 124, 202), 3, fill=silver)
        if kind == 'default':
            d.ellipse((150, 10, 236, 96), fill='#f19127', outline=light, width=5)
            d.line([(169, 53), (187, 71), (220, 34)], fill=light, width=11)
    elif kind == 'drive':
        d.polygon([(22, 98), (151, 47), (236, 86), (236, 169), (109, 217), (22, 178)], fill=silver, outline=edge, width=5)
        d.polygon([(28, 99), (151, 54), (230, 88), (108, 134)], fill=light)
        d.polygon([(109, 141), (229, 96), (229, 161), (109, 205)], fill='#c7cece')
        d.polygon([(29, 108), (101, 141), (101, 204), (29, 173)], fill='#e4e8e4')
        d.line([(43, 136), (89, 156)], fill=edge, width=7)
        d.line([(43, 160), (89, 180)], fill=silver, width=5)
        d.ellipse((176, 154, 186, 164), fill='#86b632')
    elif kind == 'folder':
        d.polygon([(20, 65), (95, 49), (122, 69), (228, 50), (226, 199), (21, 236)], fill='#799c24', outline=edge, width=4)
        d.polygon([(30, 76), (220, 58), (220, 124), (30, 150)], fill='#e7efbb')
        d.polygon([(27, 107), (231, 73), (209, 206), (15, 239)], fill=green, outline='#6e982a', width=4)
        d.polygon([(35, 112), (222, 81), (211, 102), (30, 133)], fill='#cde978')
    elif kind == 'xex':
        d.polygon([(54, 18), (161, 18), (209, 66), (209, 235), (54, 235)], fill='#86b12b', outline=edge, width=4)
        d.polygon([(161, 20), (161, 68), (208, 68)], fill='#e4efba')
        d.polygon([(111, 82), (151, 82), (151, 146), (178, 146), (131, 196), (85, 146), (111, 146)], fill=light)
        d.line((74, 216, 187, 216), fill='#bde267', width=7)
    return im

sheet = Image.new('RGB', (7*140, 172), '#202020')
draw = ImageDraw.Draw(sheet)
for i, kind in enumerate(('root', 'add', 'console', 'default', 'drive', 'folder', 'xex')):
    im = render(kind)
    im.save(OUT / (kind+'.ico'), sizes=SIZES)
    thumb = im.resize((112,112), Image.Resampling.LANCZOS)
    sheet.paste(thumb, (i*140+14,12), thumb)
    draw.text((i*140+14,137), kind, fill='white')
render('root').save(ROOT / 'src' / 'ui' / 'crackhood.ico', sizes=SIZES)
sheet.save(OUT / 'preview.png')
