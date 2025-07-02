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
#include <json-glib/json-glib.h>
#include "../providers.h"
#include "../provider_registry.h"
#include "../libchatgpt.h"

/* Shared OpenAI-compatible request formatting */
JsonObject*
openai_compat_format_request(ChatGptBuddy *buddy, const char *message)
{
    JsonObject *request;
    JsonArray *messages;
    JsonObject *msg;
    GList *history;
    
    request = json_object_new();
    messages = json_array_new();
    
    /* Add system message if configured */
    if (buddy->instructions && *buddy->instructions) {
        msg = json_object_new();
        json_object_set_string_member(msg, "role", "system");
        json_object_set_string_member(msg, "content", buddy->instructions);
        json_array_add_object_element(messages, msg);
    }
    
    /* Add conversation history */
    for (history = buddy->history; history != NULL; history = history->next) {
        ChatGptHistory *hist = (ChatGptHistory *)history->data;
        msg = json_object_new();
        json_object_set_string_member(msg, "role", hist->role);
        json_object_set_string_member(msg, "content", hist->content);
        json_array_add_object_element(messages, msg);
    }
    
    /* Add current message */
    msg = json_object_new();
    json_object_set_string_member(msg, "role", "user");
    json_object_set_string_member(msg, "content", message);
    json_array_add_object_element(messages, msg);
    
    /* Build request */
    json_object_set_string_member(request, "model", buddy->model ? buddy->model : "gpt-3.5-turbo");
    json_object_set_array_member(request, "messages", messages);
    json_object_set_double_member(request, "temperature", 0.7);
    
    return request;
}

/* Shared OpenAI-compatible response parsing */
char*
openai_compat_parse_response(JsonObject *response, GError **error)
{
    JsonArray *choices;
    JsonObject *choice;
    JsonObject *message;
    const char *content;
    
    if (!json_object_has_member(response, "choices")) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No choices in response");
        }
        return NULL;
    }
    
    choices = json_object_get_array_member(response, "choices");
    if (json_array_get_length(choices) == 0) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "Empty choices array");
        }
        return NULL;
    }
    
    choice = json_array_get_object_element(choices, 0);
    message = json_object_get_object_member(choice, "message");
    content = json_object_get_string_member(message, "content");
    
    if (content == NULL) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No content in response");
        }
        return NULL;
    }
    
    return g_strdup(content);
}

/* Shared OpenAI-compatible response validation */
gboolean
openai_compat_validate_response(JsonObject *response, GError **error)
{
    if (json_object_has_member(response, "error")) {
        JsonObject *error_obj = json_object_get_object_member(response, "error");
        const char *error_msg = json_object_get_string_member(error_obj, "message");
        const char *error_type = json_object_get_string_member(error_obj, "type");
        
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                                "API Error (%s): %s", 
                                error_type ? error_type : "unknown",
                                error_msg ? error_msg : "Unknown error");
        }
        return FALSE;
    }
    
    return TRUE;
}

/* Shared OpenAI-compatible URL building */
char*
openai_compat_get_chat_url(LLMProvider *provider, ChatGptBuddy *buddy)
{
    return g_strdup_printf("%s%s", provider->endpoint_url, provider->chat_endpoint);
}

/* Shared OpenAI-compatible headers */
GHashTable*
openai_compat_get_additional_headers(ChatGptAccount *account, ChatGptBuddy *buddy)
{
    GHashTable *headers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    const char *api_key = purple_account_get_string(account->account, "api_key", "");
    char *auth_header = g_strdup_printf("Bearer %s", api_key);
    
    g_hash_table_insert(headers, g_strdup("Content-Type"), g_strdup("application/json"));
    g_hash_table_insert(headers, g_strdup("Authorization"), auth_header);
    
    return headers;
}

/* Shared OpenAI-compatible error parsing */
char*
openai_compat_parse_error(JsonObject *response)
{
    JsonObject *error_obj;
    const char *error_msg;
    const char *error_type;
    
    if (!json_object_has_member(response, "error")) {
        return g_strdup("Unknown error");
    }
    
    error_obj = json_object_get_object_member(response, "error");
    error_msg = json_object_get_string_member(error_obj, "message");
    error_type = json_object_get_string_member(error_obj, "type");
    
    if (error_type && error_msg) {
        return g_strdup_printf("%s: %s", error_type, error_msg);
    } else if (error_msg) {
        return g_strdup(error_msg);
    } else {
        return g_strdup("Unknown error");
    }
}

/* Generic auth header function for Bearer token */
const char*
openai_compat_get_auth_header(ChatGptAccount *account)
{
    static char auth_header[512];
    const char *api_key;
    
    api_key = purple_account_get_string(account->account, "api_key", "");
    g_snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
    
    return auth_header;
}

/* Generic model feature checking (most OpenAI-compatible providers support basic features) */
gboolean
openai_compat_model_supports_feature(const char *model, const char *feature)
{
    /* Most OpenAI-compatible providers support basic features */
    if (g_strcmp0(feature, "functions") == 0) {
        return TRUE;  /* Most models support function calling */
    }
    
    if (g_strcmp0(feature, "vision") == 0) {
        /* Only specific models support vision */
        return g_strstr_len(model, -1, "vision") != NULL ||
               g_strstr_len(model, -1, "gpt-4") != NULL;
    }
    
    return FALSE;
}

/* Mistral AI models */
static const char *mistral_models[] = {
    "mistral-large-latest",
    "mistral-medium-latest", 
    "mistral-small-latest",
    "open-mistral-7b",
    "open-mixtral-8x7b",
    "open-mixtral-8x22b",
    NULL
};

/* Fireworks AI models (popular open-source models) */
static const char *fireworks_models[] = {
    "accounts/fireworks/models/llama-v3p1-70b-instruct",
    "accounts/fireworks/models/llama-v3p1-8b-instruct",
    "accounts/fireworks/models/mixtral-8x7b-instruct",
    "accounts/fireworks/models/mixtral-8x22b-instruct",
    "accounts/fireworks/models/qwen2p5-72b-instruct",
    NULL
};

/* Together AI models */
static const char *together_models[] = {
    "meta-llama/Meta-Llama-3.1-70B-Instruct-Turbo",
    "meta-llama/Meta-Llama-3.1-8B-Instruct-Turbo",
    "mistralai/Mixtral-8x7B-Instruct-v0.1",
    "mistralai/Mixtral-8x22B-Instruct-v0.1",
    "Qwen/Qwen2.5-72B-Instruct-Turbo",
    NULL
};

/* xAI models */
static const char *xai_models[] = {
    "grok-beta",
    "grok-vision-beta",
    NULL
};

/* Groq models */
static const char *groq_models[] = {
    "llama-3.1-70b-versatile",
    "llama-3.1-8b-instant",
    "mixtral-8x7b-32768",
    "gemma2-9b-it",
    NULL
};

/* DeepSeek models */
static const char *deepseek_models[] = {
    "deepseek-chat",
    "deepseek-coder",
    NULL
};

/* Provider definitions using shared functions */

static LLMProvider mistral_provider = {
    .name = "mistral",
    .display_name = "Mistral AI",
    .endpoint_url = "https://api.mistral.ai",
    .chat_endpoint = "/v1/chat/completions",
    .models = mistral_models,
    .needs_api_key = TRUE,
    .is_local = FALSE,
    .api_format = API_FORMAT_OPENAI,
    .supports_streaming = TRUE,
    .supports_vision = FALSE,
    .supports_functions = TRUE,
    .max_context_length = 32768,
    .format_request = openai_compat_format_request,
    .parse_response = openai_compat_parse_response,
    .get_auth_header = openai_compat_get_auth_header,
    .validate_response = openai_compat_validate_response,
    .get_chat_url = openai_compat_get_chat_url,
    .get_additional_headers = openai_compat_get_additional_headers,
    .parse_error = openai_compat_parse_error,
    .model_supports_feature = openai_compat_model_supports_feature
};

static LLMProvider fireworks_provider = {
    .name = "fireworks",
    .display_name = "Fireworks AI",
    .endpoint_url = "https://api.fireworks.ai",
    .chat_endpoint = "/inference/v1/chat/completions",
    .models = fireworks_models,
    .needs_api_key = TRUE,
    .is_local = FALSE,
    .api_format = API_FORMAT_OPENAI,
    .supports_streaming = TRUE,
    .supports_vision = FALSE,
    .supports_functions = TRUE,
    .max_context_length = 32768,
    .format_request = openai_compat_format_request,
    .parse_response = openai_compat_parse_response,
    .get_auth_header = openai_compat_get_auth_header,
    .validate_response = openai_compat_validate_response,
    .get_chat_url = openai_compat_get_chat_url,
    .get_additional_headers = openai_compat_get_additional_headers,
    .parse_error = openai_compat_parse_error,
    .model_supports_feature = openai_compat_model_supports_feature
};

static LLMProvider together_provider = {
    .name = "together",
    .display_name = "Together AI",
    .endpoint_url = "https://api.together.xyz",
    .chat_endpoint = "/v1/chat/completions",
    .models = together_models,
    .needs_api_key = TRUE,
    .is_local = FALSE,
    .api_format = API_FORMAT_OPENAI,
    .supports_streaming = TRUE,
    .supports_vision = FALSE,
    .supports_functions = TRUE,
    .max_context_length = 32768,
    .format_request = openai_compat_format_request,
    .parse_response = openai_compat_parse_response,
    .get_auth_header = openai_compat_get_auth_header,
    .validate_response = openai_compat_validate_response,
    .get_chat_url = openai_compat_get_chat_url,
    .get_additional_headers = openai_compat_get_additional_headers,
    .parse_error = openai_compat_parse_error,
    .model_supports_feature = openai_compat_model_supports_feature
};

static LLMProvider xai_provider = {
    .name = "xai",
    .display_name = "xAI",
    .endpoint_url = "https://api.x.ai",
    .chat_endpoint = "/v1/chat/completions",
    .models = xai_models,
    .needs_api_key = TRUE,
    .is_local = FALSE,
    .api_format = API_FORMAT_OPENAI,
    .supports_streaming = TRUE,
    .supports_vision = TRUE,  /* Grok Vision */
    .supports_functions = TRUE,
    .max_context_length = 131072,
    .format_request = openai_compat_format_request,
    .parse_response = openai_compat_parse_response,
    .get_auth_header = openai_compat_get_auth_header,
    .validate_response = openai_compat_validate_response,
    .get_chat_url = openai_compat_get_chat_url,
    .get_additional_headers = openai_compat_get_additional_headers,
    .parse_error = openai_compat_parse_error,
    .model_supports_feature = openai_compat_model_supports_feature
};

static LLMProvider groq_provider = {
    .name = "groq",
    .display_name = "Groq",
    .endpoint_url = "https://api.groq.com",
    .chat_endpoint = "/openai/v1/chat/completions",
    .models = groq_models,
    .needs_api_key = TRUE,
    .is_local = FALSE,
    .api_format = API_FORMAT_OPENAI,
    .supports_streaming = TRUE,
    .supports_vision = FALSE,
    .supports_functions = TRUE,
    .max_context_length = 32768,
    .format_request = openai_compat_format_request,
    .parse_response = openai_compat_parse_response,
    .get_auth_header = openai_compat_get_auth_header,
    .validate_response = openai_compat_validate_response,
    .get_chat_url = openai_compat_get_chat_url,
    .get_additional_headers = openai_compat_get_additional_headers,
    .parse_error = openai_compat_parse_error,
    .model_supports_feature = openai_compat_model_supports_feature
};

static LLMProvider deepseek_provider = {
    .name = "deepseek",
    .display_name = "DeepSeek",
    .endpoint_url = "https://api.deepseek.com",
    .chat_endpoint = "/v1/chat/completions",
    .models = deepseek_models,
    .needs_api_key = TRUE,
    .is_local = FALSE,
    .api_format = API_FORMAT_OPENAI,
    .supports_streaming = TRUE,
    .supports_vision = FALSE,
    .supports_functions = TRUE,
    .max_context_length = 32768,
    .format_request = openai_compat_format_request,
    .parse_response = openai_compat_parse_response,
    .get_auth_header = openai_compat_get_auth_header,
    .validate_response = openai_compat_validate_response,
    .get_chat_url = openai_compat_get_chat_url,
    .get_additional_headers = openai_compat_get_additional_headers,
    .parse_error = openai_compat_parse_error,
    .model_supports_feature = openai_compat_model_supports_feature
};

/* Initialize all OpenAI-compatible providers */
void
llm_provider_openai_compat_init(void)
{
    llm_provider_registry_register(&mistral_provider);
    llm_provider_registry_register(&fireworks_provider);
    llm_provider_registry_register(&together_provider);
    llm_provider_registry_register(&xai_provider);
    llm_provider_registry_register(&groq_provider);
    llm_provider_registry_register(&deepseek_provider);
}

