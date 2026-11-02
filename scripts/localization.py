"""Generate and validate the five NEXUS settings and installer languages."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]

def main():
    base = ROOT / 'data/locale'
    english = dict(re.findall(r'^(\w+)="(.*)"$', (base/'en-US.ini').read_text(encoding='utf-8'), re.M))
    rows = {}
    for line in (base/'translations.tsv').read_text(encoding='utf-8-sig').splitlines():
        if line:
            key, *values = line.split('|')
            assert len(values) == 3, key
            rows[key] = values
    for index, locale in enumerate(('it-IT', 'fr-FR', 'de-DE')):
        values = {key: value[index] for key, value in rows.items()}
        for key, value in english.items():
            # Preset names and broadcast-only labels retain their English names.
            if (key.startswith('Style') and key not in values) or key.endswith('Prefix') or key == 'TwitchAnonymous':
                values[key] = value
        for number in range(2, 6):
            values[f'WeatherCity{number}'] = (
                f'Città e paese {number} (facoltativo)',
                f'Ville et pays {number} (facultatif)',
                f'Stadt und Land {number} (optional)')[index]
        assert set(english) <= set(values), set(english)-set(values)
        (base/(locale+'.ini')).write_text(''.join(f'{key}="{values[key]}"\n' for key in english), encoding='utf-8')
    for locale in ('es-ES', 'en-US', 'it-IT', 'fr-FR', 'de-DE'):
        text = (base/(locale+'.ini')).read_text(encoding='utf-8')
        keys = re.findall(r'^(\w+)=', text, re.M)
        assert len(keys) == len(set(keys)) and set(keys) == set(english), locale
    installer = {}
    for row in (ROOT/'installer/translations.tsv').read_text(encoding='utf-8-sig').splitlines():
        if row:
            parts = [value.replace('\\n', '\n') for value in row.split('|')]
            assert len(parts) == 5 and all(value.strip() for value in parts), row
            installer[parts[0]] = parts
    (ROOT/'installer/strings.json').write_text(json.dumps(installer, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'PASS five languages: {len(english)} settings keys, {len(installer)} installer messages')

if __name__ == '__main__':
    main()
