# Native font instances

Inter-Regular.ttf and Inter-SemiBold.ttf are static instances of the included
Inter.ttf variable font, using optical size 14 and weights 400 and 600.
They are embedded in the Win32 launcher so GDI selects stable outlines and
weights. Inter.ttf remains available for variable-font consumers.

The Inter Project copyright and SIL Open Font License in OFL.txt apply to all
three files. Static instances can be regenerated with fontTools.varLib.instancer:

```python
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

for weight, name in [(400, 'Regular'), (600, 'SemiBold')]:
    font = TTFont('Inter.ttf')
    font = instantiateVariableFont(font, {'opsz': 14, 'wght': weight},
                                   inplace=True, updateFontNames=True)
    font.save(f'Inter-{name}.ttf')
```
