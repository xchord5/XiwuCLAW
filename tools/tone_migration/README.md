# Tone Migration Layer

This folder provides the migration bridge:

- `tones/factory/*.json` (Xiwu ToneSpec v1)
- `tools/tone_migration/tone_json_to_faust.py` (single conversion)
- `tools/tone_migration/batch_convert_factory.py` (batch conversion)

## Convert a single tone

```powershell
python tools/tone_migration/tone_json_to_faust.py --in tones/factory/layered_bass_eq4.json --out faust/generated/layered_bass_eq4.dsp
```

## Convert all factory tones

```powershell
python tools/tone_migration/batch_convert_factory.py
```

The resulting Faust DSP files can later be compiled into plugin modules/caches.
