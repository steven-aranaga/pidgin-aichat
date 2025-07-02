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

#ifndef _PROVIDER_REGISTRY_H_
#define _PROVIDER_REGISTRY_H_

#include "providers.h"

/* Register a provider with the registry */
gboolean llm_provider_registry_register(LLMProvider *provider);

/* Unregister a provider from the registry */
gboolean llm_provider_registry_unregister(LLMProviderType type);

/* Get a provider from the registry */
LLMProvider* llm_provider_registry_get(LLMProviderType type);

/* Get a provider by name from the registry */
LLMProvider* llm_provider_registry_get_by_name(const char *name);

/* Get all registered providers */
GList* llm_provider_registry_get_all(void);

/* Initialize the provider registry */
void llm_provider_registry_init(void);

/* Cleanup the provider registry */
void llm_provider_registry_uninit(void);

/* Check if a provider is registered */
gboolean llm_provider_registry_is_registered(LLMProviderType type);

/* Get the number of registered providers */
guint llm_provider_registry_count(void);

#endif /* _PROVIDER_REGISTRY_H_ */