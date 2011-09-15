
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2011, the olsr.org team - see HISTORY file
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "common/common_types.h"
#include "config/cfg_schema.h"
#include "config/cfg_delta.h"

#include "olsr_logging.h"
#include "olsr_plugins.h"
#include "olsr_cfg.h"
#include "olsr.h"

/* static prototypes */
static int _validate_global(struct cfg_schema_section *, const char *section_name,
    struct cfg_named_section *, struct autobuf *);

/* global config */
struct olsr_config_global config_global;

static struct cfg_db *olsr_raw_db = NULL;
static struct cfg_db *olsr_work_db = NULL;
static struct cfg_schema olsr_schema;
static struct cfg_delta olsr_delta;

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(olsr_cfg_state);

/* define global configuration template */
static struct cfg_schema_section global_section = {
  .t_type = CFG_SECTION_GLOBAL,
  .t_validate = _validate_global,
};

static struct cfg_schema_entry global_entries[] = {
  CFG_MAP_BOOL(olsr_config_global, fork, "no",
      "Set to true to fork daemon into background."),
  CFG_MAP_BOOL(olsr_config_global, failfast, "no",
      "Set to true to stop daemon statup if at least one plugin doesn't load."),
  CFG_MAP_BOOL(olsr_config_global, ipv4, "yes",
      "Set to true to enable ipv4 support in program."),
  CFG_MAP_BOOL(olsr_config_global, ipv6, "yes",
      "Set to true to enable ipv6 support in program."),

  CFG_MAP_STRINGLIST(olsr_config_global, plugin, "",
      "Set list of plugins to be loaded by daemon. Some might need configuration options."),
};

/**
 * Initializes the olsrd configuration subsystem
 */
int
olsr_cfg_init(void) {
  if (olsr_subsystem_init(&olsr_cfg_state))
    return 0;

  /* initialize schema */
  cfg_schema_add(&olsr_schema);
  cfg_schema_add_section(&olsr_schema, &global_section);
  cfg_schema_add_entries(&global_section, global_entries, ARRAYSIZE(global_entries));

  /* initialize database */
  if ((olsr_raw_db = cfg_db_add()) == NULL) {
    OLSR_WARN(LOG_CONFIG, "Cannot create raw configuration database.");
    olsr_subsystem_cleanup(&olsr_cfg_state);
    return -1;
  }

  /* initialize database */
  if ((olsr_work_db = cfg_db_add()) == NULL) {
    OLSR_WARN(LOG_CONFIG, "Cannot create configuration database.");
    cfg_db_remove(olsr_raw_db);
    olsr_subsystem_cleanup(&olsr_cfg_state);
    return -1;
  }

  cfg_db_link_schema(olsr_raw_db, &olsr_schema);

  /* initialize delta */
  cfg_delta_add(&olsr_delta);

  /* initialize global config */
  memset(&config_global, 0, sizeof(config_global));
  return 0;
}

/**
 * Cleans up all data allocated by the olsrd configuration subsystem
 */
void
olsr_cfg_cleanup(void) {
  if (olsr_subsystem_cleanup(&olsr_cfg_state))
    return;

  free(config_global.plugin.value);
  cfg_delta_remove(&olsr_delta);

  cfg_db_remove(olsr_raw_db);
  cfg_db_remove(olsr_work_db);

  cfg_schema_remove(&olsr_schema);
}

/**
 * Applies to content of the raw configuration database into the
 * work database and triggers the change calculation.
 * @return 0 if successful, -1 otherwise
 */
int
olsr_cfg_apply(void) {
  struct olsr_plugin *plugin, *plugin_it;
  struct cfg_db *new_db, *old_db;
  struct cfg_entry *entry;
  struct autobuf log;
  bool found;
  int result;
  const char *ptr;

  if (abuf_init(&log, 0)) {
    OLSR_WARN_OOM(LOG_CONFIG);
    return -1;
  }

  /*** phase 1: activate all plugins ***/
  result = -1;
  old_db = NULL;
  entry = NULL;

  /* load plugins */
  CFG_FOR_ALL_STRINGS(&config_global.plugin, ptr) {
    if (olsr_plugins_load(ptr) == NULL && config_global.failfast) {
      goto apply_failed;
    }
  }

  /* unload all plugins that are not in use anymore */
  OLSR_FOR_ALL_PLUGIN_ENTRIES(plugin, plugin_it) {
    found = false;

    /* search if plugin should still be active */
    CFG_FOR_ALL_STRINGS(&config_global.plugin, ptr) {
      if (olsr_plugins_get(ptr) == plugin) {
        found = true;
        break;
      }
    }

    if (!found && !olsr_plugins_is_static(plugin)) {
      /* if not, unload it (if not static) */
      olsr_plugins_unload(plugin);
    }
  }

  /*** phase 2: check configuration and apply it ***/
  /* re-validate configuration data */
  if (cfg_schema_validate(olsr_raw_db, config_global.failfast, false, true, &log)) {
    OLSR_WARN(LOG_CONFIG, "Configuration validation failed");
    OLSR_WARN_NH(LOG_CONFIG, "%s", log.buf);
    goto apply_failed;
  }

  /* create new configuration database with correct values */
  new_db = cfg_db_duplicate(olsr_raw_db);
  if (new_db == NULL) {
    OLSR_WARN_OOM(LOG_CONFIG);
    goto apply_failed;
  }

  /* switch databases */
  old_db = olsr_work_db;
  olsr_work_db = new_db;

  /* bind schema */
  cfg_db_link_schema(olsr_work_db, &olsr_schema);

  /* enable all plugins */
  OLSR_FOR_ALL_PLUGIN_ENTRIES(plugin, plugin_it) {
    if (!plugin->int_enabled && olsr_plugins_enable(plugin) != 0
        && config_global.failfast) {
      goto apply_failed;
    }
  }

  /* remove everything not valid */
  cfg_schema_validate(new_db, false, true, false, NULL);

  olsr_cfg_update_globalcfg(false);

  /* calculate delta and call handlers */
  cfg_delta_calculate(&olsr_delta, old_db, olsr_work_db);

  /* success */
  result = 0;

apply_failed:
  /* look for loaded but not enabled plugins and unload them */
  OLSR_FOR_ALL_PLUGIN_ENTRIES(plugin, plugin_it) {
    if (plugin->int_loaded && !plugin->int_enabled) {
      olsr_plugins_unload(plugin);
    }
  }

  if (old_db) {
    cfg_db_remove(old_db);
  }
  abuf_free(&log);
  return result;
}

int
olsr_cfg_update_globalcfg(bool raw) {
  struct cfg_named_section *named;

  named = cfg_db_find_namedsection(
      raw ? olsr_raw_db : olsr_work_db, CFG_SECTION_GLOBAL, NULL);

  return cfg_schema_tobin(&config_global,
      named, global_entries, ARRAYSIZE(global_entries));
}

/**
 * This function will clear the raw configuration database
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_cfg_create_new_rawdb(void) {
  /* free old db */
  cfg_db_remove(olsr_raw_db);

  /* initialize database */
  if ((olsr_raw_db = cfg_db_add()) == NULL) {
    OLSR_WARN(LOG_CONFIG, "Cannot create raw configuration database.");
    olsr_exit();
    return -1;
  }

  cfg_db_link_schema(olsr_raw_db, &olsr_schema);
  return 0;
}

/**
 * @return pointer to olsr configuration database
 */
struct cfg_db *
olsr_cfg_get_db(void) {
  return olsr_work_db;
}

/**
 * @return pointer to olsr raw configuration database
 */
struct cfg_db *
olsr_cfg_get_rawdb(void) {
  return olsr_raw_db;
}

/**
 * @return pointer to olsr configuration schema
 */
struct cfg_schema *
olsr_cfg_get_schema(void) {
  return &olsr_schema;
}

/**
 * @return pointer to olsr configuration delta management
 */
struct cfg_delta *
olsr_cfg_get_delta(void) {
  return &olsr_delta;
}

/**
 * Validates if the settings of the global section are
 * consistent.
 * @param schema pointer to section schema
 * @param section_name name of section
 * @param section pointer to named section of database
 * @param log pointer to logging output buffer
 * @return -1 if section is not valid, 0 otherwise
 */
static int
_validate_global(struct cfg_schema_section *schema __attribute__((unused)),
    const char *section_name __attribute__((unused)),
    struct cfg_named_section *section, struct autobuf *log) {
  struct olsr_config_global config;

  memset(&config, 0, sizeof(config));

  if (cfg_schema_tobin(&config,
        section, global_entries, ARRAYSIZE(global_entries))) {
    cfg_append_printable_line(log, "Could not generate binary template of global section");
    return -1;
  }

  if (!config.ipv4 && !config.ipv6) {
    cfg_append_printable_line(log, "You have to activate either ipv4 or ipv6 (or both)");
    return -1;
  }

  free(config.plugin.value);
  return 0;
}
