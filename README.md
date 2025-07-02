# AI Chat plugin for Pidgin

A Pidgin/libpurple plugin that lets you create and chat with AI bots from various Large Language Model (LLM) providers.
Supports 13+ major LLM providers out of the box including OpenAI, Anthropic, Google Gemini, Mistral, and many more, plus local models via Ollama and custom endpoints.

## Credits

This project is a fork and extension of the original [pidgin-chatgpt](https://github.com/EionRobb/pidgin-chatgpt) plugin by [Eion Robb](https://github.com/EionRobb). The original plugin provided OpenAI ChatGPT support, and this fork extends it to support multiple LLM providers.

[Zawinski's Law](https://en.wikipedia.org/wiki/Jamie_Zawinski#Zawinski's_Law) says "Every program attempts to expand until it can read mail."

These days it should be "Every program attempts to expand until it can large language model."

## Photographic evidence of the crime

![image](https://github.com/user-attachments/assets/92b3b963-bec7-426b-a149-29b894081f83)


## Supported LLM Providers

### Cloud Providers (Pre-configured)
- **OpenAI** (GPT-4, GPT-3.5-Turbo) - Requires API key from https://platform.openai.com/api-keys
- **Anthropic** (Claude 3 Opus/Sonnet/Haiku) - Requires API key from https://console.anthropic.com/
- **Google Gemini** (Gemini Pro, Gemini 1.5) - Requires API key from https://makersuite.google.com/app/apikey
- **Mistral AI** (Mistral Large/Medium/Small) - Requires API key from https://console.mistral.ai/
- **Fireworks AI** (Llama, Mixtral, etc.) - Requires API key from https://app.fireworks.ai/
- **Together AI** (Open source models) - Requires API key from https://api.together.xyz/
- **xAI** (Grok) - Requires API key from https://x.ai/
- **OpenRouter** (Multi-provider gateway) - Requires API key from https://openrouter.ai/
- **Groq** (Fast inference) - Requires API key from https://console.groq.com/
- **DeepSeek** (DeepSeek Chat/Coder) - Requires API key from https://platform.deepseek.com/
- **Hugging Face** (Various models) - Requires API token from https://huggingface.co/settings/tokens
- **Cohere** (Command models) - Requires API key from https://dashboard.cohere.com/

### Local Providers
- **Ollama** - Run models locally, no API key required. Install from https://ollama.ai/

### Custom LLM Support
Configure any OpenAI-compatible endpoint or implement custom adapters for other API formats.

## Custom LLM Integration

### For OpenAI-Compatible APIs
Many providers offer OpenAI-compatible endpoints. To use these:

1. **API Endpoint URL** - The base URL for the LLM's API
2. **Authentication** - API key (Bearer token format)
3. **Model Name** - The specific model identifier

Example:
```
Endpoint: https://your-provider.com/v1
API Key: your-api-key
Model: your-model-name
```

### For Non-Compatible APIs
The plugin includes adapters for:
- Anthropic Messages API format
- Google GenerateContent API format
- Cohere Chat API format
- Ollama local API format

Custom adapters can be implemented for other formats.

## Installation

### From Source
```bash
make
make install
make install-icons
```

### Dependencies
- libpurple development headers
- json-glib
- glib2 development headers
- C compiler (gcc/clang)

## Configuration

1. Add a new account in Pidgin with protocol "AI Chat"
2. Enter your API key for your chosen provider
3. Select your LLM provider from the account options
4. Create new "buddies" which represent different AI bots with their own configurations

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for:
- Adding support for new LLM providers
- Improving the provider abstraction layer
- Bug fixes and performance improvements
- Documentation improvements

## License

This project maintains the original GPL v3+ license. See LICENSE file for details.
