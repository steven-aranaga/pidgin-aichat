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

#ifndef _PROVIDERS_H_
#define _PROVIDERS_H_

#include <glib.h>
#include <json-glib/json-glib.h>

/* Forward declarations */
typedef struct _AiChatAccount AiChatAccount;
typedef struct _AiChatBuddy AiChatBuddy;

/* Provider types enum */
typedef enum {
    LLM_PROVIDER_OPENAI,
    LLM_PROVIDER_ANTHROPIC,
    LLM_PROVIDER_GOOGLE,
    LLM_PROVIDER_MISTRAL,
    LLM_PROVIDER_FIREWORKS,
    LLM_PROVIDER_TOGETHER,
    LLM_PROVIDER_XAI,
    LLM_PROVIDER_OPENROUTER,
    LLM_PROVIDER_GROQ,
    LLM_PROVIDER_DEEPSEEK,
    LLM_PROVIDER_HUGGINGFACE,
    LLM_PROVIDER_COHERE,
    LLM_PROVIDER_OLLAMA,
    LLM_PROVIDER_CUSTOM,
    LLM_PROVIDER_COUNT
} LLMProviderType;

/* API format types */
typedef enum {
    API_FORMAT_OPENAI,      /* OpenAI-compatible format */
    API_FORMAT_ANTHROPIC,   /* Anthropic Messages format */
    API_FORMAT_GOOGLE,      /* Google GenerateContent format */
    API_FORMAT_COHERE,      /* Cohere Chat format */
    API_FORMAT_OLLAMA,      /* Ollama-specific format */
    API_FORMAT_CUSTOM       /* Custom format requiring full adapter */
} LLMApiFormat;

/* Provider configuration structure */
typedef struct _LLMProvider {
    /* Basic provider information */
    const char *name;           /* Internal provider name (e.g., "openai", "anthropic") */
    const char *display_name;   /* User-friendly display name */
    const char *endpoint_url;   /* Base API URL */
    const char *chat_endpoint;  /* Chat completion endpoint path */
    
    /* Provider characteristics */
    const char **models;        /* NULL-terminated array of supported models */
    gboolean needs_api_key;     /* Whether API key is required */
    gboolean is_local;          /* Whether this is a local provider (e.g., Ollama) */
    LLMApiFormat api_format;    /* API format type for easier handling */
    
    /* Provider capabilities */
    gboolean supports_streaming;    /* Whether provider supports streaming responses */
    gboolean supports_vision;       /* Whether provider supports image inputs */
    gboolean supports_functions;    /* Whether provider supports function calling */
    gint max_context_length;        /* Maximum context window size (0 = unknown) */
    
    /* Function pointers for provider-specific implementations */
    
    /* Format a chat request for this provider */
    JsonObject* (*format_request)(AiChatBuddy *buddy, const char *message);
    
    /* Parse a response from this provider */
    char* (*parse_response)(JsonObject *response, GError **error);
    
    /* Get the authentication header for this provider */
    const char* (*get_auth_header)(AiChatAccount *account);
    
    /* Validate a response from this provider */
    gboolean (*validate_response)(JsonObject *response, GError **error);
    
    /* Get the full URL for a chat request */
    char* (*get_chat_url)(struct _LLMProvider *provider, AiChatBuddy *buddy);
    
    /* Get additional headers if needed */
    GHashTable* (*get_additional_headers)(AiChatAccount *account, AiChatBuddy *buddy);
    
    /* Parse error response */
    char* (*parse_error)(JsonObject *response);
    
    /* Check if model supports a specific feature */
    gboolean (*model_supports_feature)(const char *model, const char *feature);
    
} LLMProvider;

/* Provider interface functions */

/* Get a provider by type */
LLMProvider* llm_provider_get(LLMProviderType type);

/* Get a provider by name */
LLMProvider* llm_provider_get_by_name(const char *name);

/* Get all available providers */
GList* llm_provider_get_all(void);

/* Get display name for a provider type */
const char* llm_provider_get_display_name(LLMProviderType type);

/* Get provider type from name */
LLMProviderType llm_provider_get_type_from_name(const char *name);

/* Check if a provider is available */
gboolean llm_provider_is_available(LLMProviderType type);

/* Get models for a provider */
const char** llm_provider_get_models(LLMProviderType type);

/* Provider capability checks */
gboolean llm_provider_supports_streaming(LLMProviderType type);
gboolean llm_provider_supports_vision(LLMProviderType type);
gboolean llm_provider_supports_functions(LLMProviderType type);

/* Initialize the provider system */
void llm_providers_init(void);

/* Cleanup the provider system */
void llm_providers_uninit(void);

/* Provider type names array */
extern const char *provider_type_names[];

#endif /* _PROVIDERS_H_ */
