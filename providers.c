/*
 * pidgin-aichat
 *
 * Copyright (C) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#include <glib.h>
#include <string.h>
#include "providers.h"
#include "provider_registry.h"

/* Provider type to name mapping */
const char *provider_type_names[] = {
    "openai",
    "anthropic",
    "google",
    "mistral",
    "fireworks",
    "together",
    "xai",
    "openrouter",
    "groq",
    "deepseek",
    "huggingface",
    "cohere",
    "ollama",
    "custom"
};

/* Provider type to display name mapping */
static const char *provider_display_names[] = {
    "OpenAI",
    "Anthropic",
    "Google Gemini",
    "Mistral AI",
    "Fireworks AI",
    "Together AI",
    "xAI",
    "OpenRouter",
    "Groq",
    "DeepSeek",
    "Hugging Face",
    "Cohere",
    "Ollama (Local)",
    "Custom Provider"
};

/* Forward declarations for provider initialization functions */
extern void llm_provider_openai_init(void);
extern void llm_provider_anthropic_init(void);
extern void llm_provider_google_init(void);
extern void llm_provider_openai_compat_init(void);
extern void llm_provider_openrouter_init(void);
extern void llm_provider_huggingface_init(void);
extern void llm_provider_cohere_init(void);
extern void llm_provider_ollama_init(void);
extern void llm_provider_custom_init(void);

/* Initialize the provider system */
void
llm_providers_init(void)
{
    /* Initialize the registry */
    llm_provider_registry_init();
    
    /* Register built-in providers */
    llm_provider_openai_init();
    llm_provider_anthropic_init();
    llm_provider_google_init();
    llm_provider_openai_compat_init();  /* Registers Mistral, Fireworks, Together, xAI, Groq, DeepSeek */
    
    /* Register Phase 3 providers */
    llm_provider_openrouter_init();
    llm_provider_huggingface_init();
    llm_provider_cohere_init();
    llm_provider_ollama_init();
    llm_provider_custom_init();
}

/* Cleanup the provider system */
void
llm_providers_uninit(void)
{
    llm_provider_registry_uninit();
}

/* Get a provider by type */
LLMProvider*
llm_provider_get(LLMProviderType type)
{
    return llm_provider_registry_get(type);
}

/* Get a provider by name */
LLMProvider*
llm_provider_get_by_name(const char *name)
{
    return llm_provider_registry_get_by_name(name);
}

/* Get all available providers */
GList*
llm_provider_get_all(void)
{
    return llm_provider_registry_get_all();
}

/* Get display name for a provider type */
const char*
llm_provider_get_display_name(LLMProviderType type)
{
    if (type >= LLM_PROVIDER_COUNT) {
        return NULL;
    }
    
    return provider_display_names[type];
}

/* Get provider type from name */
LLMProviderType
llm_provider_get_type_from_name(const char *name)
{
    int i;
    
    if (name == NULL) {
        return LLM_PROVIDER_COUNT;
    }
    
    for (i = 0; i < LLM_PROVIDER_COUNT; i++) {
        if (g_strcmp0(provider_type_names[i], name) == 0) {
            return (LLMProviderType)i;
        }
    }
    
    return LLM_PROVIDER_COUNT;
}

/* Check if a provider is available */
gboolean
llm_provider_is_available(LLMProviderType type)
{
    return llm_provider_registry_is_registered(type);
}

/* Get models for a provider */
const char**
llm_provider_get_models(LLMProviderType type)
{
    LLMProvider *provider = llm_provider_get(type);
    
    if (provider == NULL) {
        return NULL;
    }
    
    return provider->models;
}

/* Provider capability checks */
gboolean
llm_provider_supports_streaming(LLMProviderType type)
{
    LLMProvider *provider = llm_provider_get(type);
    
    if (provider == NULL) {
        return FALSE;
    }
    
    return provider->supports_streaming;
}

gboolean
llm_provider_supports_vision(LLMProviderType type)
{
    LLMProvider *provider = llm_provider_get(type);
    
    if (provider == NULL) {
        return FALSE;
    }
    
    return provider->supports_vision;
}

gboolean
llm_provider_supports_functions(LLMProviderType type)
{
    LLMProvider *provider = llm_provider_get(type);
    
    if (provider == NULL) {
        return FALSE;
    }
    
    return provider->supports_functions;
}
