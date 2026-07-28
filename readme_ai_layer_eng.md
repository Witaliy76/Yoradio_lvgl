[Русская версия](readme_ai_layer_rus.md)

# AI Layer in Yoradio

Author: Witaliy76 - https://github.com/Witaliy76

## What is AI Layer
AI Layer is the author's experiment in "bringing the device to life":
a quiet semantic layer that can appear behind the music and add meaning when appropriate.
It is not an assistant, it does not hold a dialogue, and it does not seek constant presence.
If the layer is silent, that is a correct state and a normal mode of operation.

The device remains a Wi-Fi internet radio: music is primary, screen and text are secondary.

## Behavior and principles
AI Layer does not initiate events and does not affect system behavior.
It appears episodically and does not compete with the music.
Weak or uncertain phrasing is not shown.

No output from AI Layer is not an error.
When input is missing or confidence is low, no output is produced.

## AI Layer layers
AI Layer may display several independent lines:

- **Facts** — short, verifiable facts about the track or artist.  
- **Interpretation** — a soft interpretation of the musical atmosphere, without analysis or explanations.  
- **Moment / Context** — neutral device context (not an AI layer): short system phrases not related to music.

Each layer can be empty independently of the others.
Layers do not have to appear at the same time and may be absent most of the time.
Moment / Context acts as a neutral fallback when AI output is not produced.
Moment / Context output conditions:
- there is a valid track (not a system status);
- AI is enabled and activated;
- a prompt is loaded;
- no Facts/Interpretation was shown for this track;
- a decision has already been made that the LLM is silent for this track.
Moment / Context is shown once per track, and when AI is off or no prompt is loaded, this layer is silent as well.

## Requirements and compatibility
AI Layer works with OpenAI-compatible API (HTTP/HTTPS).
Public and local compatible providers are supported.
Without access to a provider, the system continues to work as a regular internet radio.
On network errors or timeouts, AI Layer simply outputs nothing and does not affect device behavior.

## Getting an API key

AI Layer requires an API key for an OpenAI-compatible provider.
You can use any compatible service — public or local.

Below are a few common options.

### DeepSeek
DeepSeek provides an OpenAI-compatible API.

1. Go to: https://platform.deepseek.com/
2. Create an account or sign in.
3. Generate an API key in your dashboard.
4. Use the following settings as a starting point:
   - API host: `api.deepseek.com`
   - API port: `443`
   - API path: `/v1`

DeepSeek is suitable for experimentation and regular use.
Pricing and limits depend on your account.

### OpenAI
OpenAI provides the reference OpenAI API.

1. Go to: https://platform.openai.com/
2. Sign in and open the API keys section.
3. Create a new API key.
4. Typical settings:
   - API host: `api.openai.com`
   - API port: `443`
   - API path: `/v1`

An active billing account may be required.

### OpenRouter (optional)
OpenRouter aggregates multiple OpenAI-compatible models behind a single API.

1. Go to: https://openrouter.ai/
2. Create an account.
3. Generate an API key.
4. Typical settings:
   - API host: `openrouter.ai`
   - API port: `443`
   - API path: `/api/v1`

OpenRouter may provide a small free quota for testing,
depending on current platform policy.
Check their website for up-to-date limits.

### Notes

- The Yoradio project is not affiliated with any provider listed above.
- API availability, pricing, and limits are defined by the provider.
- If no API key is configured, AI Layer remains silent and the device works normally.
- AI Layer does not send requests automatically and does not consume API until a suitable context appears.

After getting an API key and a prompt, you can proceed to configure the layer in WebUI.

## AI Layer setup in WebUI
The settings section is located in WebUI on the AI Layer tab.

![AI Layer settings](settings_ai.jpg)

Below is the actual AI Layer settings screen:

Parameters on the screen:

- **AI enabled** — enables or disables AI Layer.  
  - Enabled: the layer may produce output.  
  - Disabled: the layer is silent, system behavior does not change.  
  - If disabled, all other parameters are ignored.

- **API host** — address of the OpenAI-compatible server.  
  - Example: `api.deepseek.com`, `api.openai.com`.  
  - If not set, requests are not sent and the layer is silent.

- **API port** — server port.  
  - Usually `443` for HTTPS.  
  - If not set, the connection is not made.

- **API path** — API path.  
  - Example: `/v1`.  
  - If not set, requests are not sent.

- **Timeout (ms)** — request timeout in milliseconds.  
  - Lower values reduce waiting time.  
  - If not set, the system default is used.

- **API key** — access key for the provider.  
  - If missing or invalid, requests are rejected and the layer is silent.  
  - Missing key does not affect radio operation.

- **Model** — provider model.  
  - Example: `deepseek-chat`, `gpt-4o-mini`.  
  - If not set, requests are not executed.

- **Prompt file** / **Upload Prompt File** — uploads a prompt file.  
  - After selecting a file, the status **Loaded (… bytes)** should appear.  
  - If no prompt is loaded, AI Layer is completely silent.

- **Apply** — saves parameters and applies them.  
  - Without pressing it, parameters are not saved.

Output language is defined by the prompt text: choose an RU / EN / PL / SK template from the `ai/` directory.
Start the prompt in the target language — that will be the response language.
Interface language (`L10N_LANGUAGE`) affects only the Moment / Context lines and does not select the prompt automatically.

## Prompt file
The prompt defines the rules and tone of the output.
It is uploaded via the **Upload Prompt File** button in WebUI.
Example prompt files from `ai/`: [ai/ai_prompt_ru.txt](ai/ai_prompt_ru.txt), [ai/ai_prompt_en.txt](ai/ai_prompt_en.txt), [ai/ai_prompt_pl.txt](ai/ai_prompt_pl.txt), [ai/ai_prompt_sk.txt](ai/ai_prompt_sk.txt). Catalog notes: [ai/README.md](ai/README.md).
Prompt structure details: [readme_ai_prompt_explained_eng.md](readme_ai_prompt_explained_eng.md).
If you pre-flash LittleFS, place the prompt at `data/ai/ai_prompt.txt`
(you can simply copy one of the files from `ai/`).
Or upload the prompt later via WebUI — the result is the same.

Rules:

- A new prompt replaces the previous one.  
- After upload, the status **Loaded (… bytes)** should appear.  
- **No prompt → AI Layer is completely silent.**  
- Missing prompt is not an error.  
- No fallback texts are used.

Maximum prompt file size: 8192 bytes (8 KB).

## Output examples
- Facts: `Alphaville — Forever Young (1984).`  
- Interpretation: `Warm, calm atmosphere, no tension.`  
- Moment: `Everything moves at its own pace.`  
- No output: `—` (screen without AI Layer lines, this is normal).

## How to know AI Layer works correctly
AI Layer is considered configured if:

- It is enabled in WebUI and parameters are saved via **Apply**.  
- A prompt is loaded and the status **Loaded (… bytes)** is shown.  
- Facts or Interpretation lines appear sometimes, and the layer is silent the rest of the time.

If the layer is always silent, check: AI Layer enabled, prompt present, API key, and model.
For normal operation, no additional diagnostics are required.
For developers and debugging, enable `AI_LAYER_DEBUG` in `myoptions.h` (value `1`).
Even when fully configured, silence remains an acceptable state.

AI Layer does not make the device “smart” — it makes it slightly more alive when appropriate.