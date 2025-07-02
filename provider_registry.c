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
#include "provider_registry.h"
#include "providers.h"

/* Global provider registry */
static GHashTable *provider_registry = NULL;
static GList *provider_list = NULL;

/* Initialize the provider registry */
void
llm_provider_registry_init(void)
{
    if (provider_registry != NULL) {
        return;
    }
    
    provider_registry = g_hash_table_new_full(
        g_direct_hash,
        g_direct_equal,
        NULL,
        NULL  /* Providers are statically allocated, don't free */
    );
}

/* Cleanup the provider registry */
void
llm_provider_registry_uninit(void)
{
    if (provider_registry != NULL) {
        g_hash_table_destroy(provider_registry);
        provider_registry = NULL;
    }
    
    if (provider_list != NULL) {
        g_list_free(provider_list);
        provider_list = NULL;
    }
}

/* Register a provider with the registry */
gboolean
llm_provider_registry_register(LLMProvider *provider)
{
    LLMProviderType type;
    
    if (provider_registry == NULL || provider == NULL) {
        return FALSE;
    }
    
    /* Determine provider type from name */
    type = llm_provider_get_type_from_name(provider->name);
    if (type >= LLM_PROVIDER_COUNT) {
        return FALSE;
    }
    
    /* Check if already registered */
    if (g_hash_table_lookup(provider_registry, GINT_TO_POINTER(type)) != NULL) {
        return FALSE;
    }
    
    /* Add to registry */
    g_hash_table_insert(provider_registry, GINT_TO_POINTER(type), provider);
    
    /* Add to list */
    provider_list = g_list_append(provider_list, provider);
    
    return TRUE;
}

/* Unregister a provider from the registry */
gboolean
llm_provider_registry_unregister(LLMProviderType type)
{
    LLMProvider *provider;
    
    if (provider_registry == NULL || type >= LLM_PROVIDER_COUNT) {
        return FALSE;
    }
    
    provider = g_hash_table_lookup(provider_registry, GINT_TO_POINTER(type));
    if (provider == NULL) {
        return FALSE;
    }
    
    /* Remove from list */
    provider_list = g_list_remove(provider_list, provider);
    
    /* Remove from registry */
    return g_hash_table_remove(provider_registry, GINT_TO_POINTER(type));
}

/* Get a provider from the registry */
LLMProvider*
llm_provider_registry_get(LLMProviderType type)
{
    if (provider_registry == NULL || type >= LLM_PROVIDER_COUNT) {
        return NULL;
    }
    
    return g_hash_table_lookup(provider_registry, GINT_TO_POINTER(type));
}

/* Get a provider by name from the registry */
LLMProvider*
llm_provider_registry_get_by_name(const char *name)
{
    GList *iter;
    
    if (provider_registry == NULL || name == NULL) {
        return NULL;
    }
    
    for (iter = provider_list; iter != NULL; iter = iter->next) {
        LLMProvider *provider = (LLMProvider *)iter->data;
        if (g_strcmp0(provider->name, name) == 0) {
            return provider;
        }
    }
    
    return NULL;
}

/* Get all registered providers */
GList*
llm_provider_registry_get_all(void)
{
    return g_list_copy(provider_list);
}

/* Check if a provider is registered */
gboolean
llm_provider_registry_is_registered(LLMProviderType type)
{
    if (provider_registry == NULL || type >= LLM_PROVIDER_COUNT) {
        return FALSE;
    }
    
    return g_hash_table_lookup(provider_registry, GINT_TO_POINTER(type)) != NULL;
}

/* Get the number of registered providers */
guint
llm_provider_registry_count(void)
{
    if (provider_registry == NULL) {
        return 0;
    }
    
    return g_hash_table_size(provider_registry);
}