[Русская версия](readme_ai_prompt_explained_rus.md)

# AI Layer prompt: how to read and change it

Author: Witaliy76 - https://github.com/Witaliy76

## Why AI Layer needs a prompt
The prompt defines AI Layer behavior and strictly limits the response format.
Without these limits, the layer becomes talkative and starts competing with music.
This document covers only AI Layer responses (Facts/Interpretation).
Moment / Context lines are not produced by the prompt and are not part of AI output.

## Core principle: silence is more important than text
Silence is a valid and desired outcome.
The prompt allows `ok=false` when there is no context or a neutral sound description cannot be made safely.
If a neutral description is possible, `mode="listen"` is preferred over silence.

## Response format (JSON)
The format is fixed by the prompt and must be followed exactly:

- `{"ok": true, "mode": "fact"|"listen", "text": "...", "confidence": 0.0-1.0}`
- `{"ok": false}` if it is better to stay silent

This is not style but a technical contract. Breaking the format breaks output.

## Response fields and their meaning
- **ok** — whether there is any output at all. `false` means full silence.  
- **mode** — output type: `fact` or `listen`.  
- **text** — one short line.  
- **confidence** — admissibility filter (see below).

## mode: fact vs mode: listen — the essential difference
The prompt enforces a strict separation:

- **fact** — only a widely known, verifiable fact.  
  Any description of sound, tempo, mood, genre, or vocals is NOT a fact.
- **listen** — a short neutral phrase about sound or mood.  
  This is not advice, not evaluation, and not a “personal opinion.”

If unsure between `fact` and `listen`, choose `listen`.

## confidence: why it exists
`confidence` is not “model confidence.” It is a filter that allows or blocks output.
The prompt sets thresholds:

- for regular facts — **0.85+**  
- for “dangerous” facts (awards/charts/sales/records) — **0.95+**  
- for movies/series — **0.99**

For `listen`, a lower range is acceptable (**0.60–0.85**) because it is a subjective sound description.

## When ok=false is acceptable
`ok=false` is allowed only in two cases stated by the prompt:

- no meaning or context (empty/broken title, not music, garbage);
- it is impossible to safely describe the sound even neutrally.

In all other cases, `mode="listen"` is preferred.

## What the prompt forbids and why
These restrictions keep the layer from turning into dialogue or repeating metadata.

Prohibited:
- emojis;  
- addressing the listener;  
- “now playing” or mentioning the radio station;  
- retelling the track title or artist name;  
- multiple sentences (one short line is required).

Also required:
- language is fixed (EN in this prompt);  
- one line;  
- only quotes `"` and hyphen `-` (no em dash).

Maximum prompt file size: 8192 bytes (8 KB).
If needed, the limit can be changed in `src/src/plugins/ai/ai_prompt.cpp` (variable `AI_PROMPT_MAX_LEN`).

## What you can change safely
Safe changes are those that keep the rules and the response contract intact.
Typically safe:

- adjust tone (more neutral or more restrained);
- make `listen` slightly more poetic or more plain;
- tighten fact selection;
- switch language (RU/EN) when using the corresponding prompt.

## What is NOT recommended to change
These elements define the behavior and should remain intact.

- **JSON format** — this is the contract with the device.  
- **Single-line output** — prevents the layer from becoming talkative.  
- **Mode selection rule** — prevents `fact` from describing sound.  
- **ok=false rule** — controls how often silence is used.  
- **Priority of silence** — no output is better than weak output.

This prompt does not make AI “smart” — it makes output rare, appropriate, and safe for background listening.
