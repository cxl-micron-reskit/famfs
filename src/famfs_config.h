/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2023-2025 Micron Technology, Inc.  All rights reserved.
 *
 * famfs configuration system - git-style hierarchical configuration
 *
 * Config files are loaded in order (system -> user), with later files
 * overriding earlier ones. Command-line options override config values.
 */

#ifndef _H_FAMFS_CONFIG
#define _H_FAMFS_CONFIG

#include <stdio.h>
#include <stdbool.h>
#include "famfs.h"

/* Forward declaration */
struct famfs_interleave_param;

/*
 * Config file locations (in load order, lowest to highest priority):
 *   1. /etc/famfs.conf         - system-wide config
 *   2. ~/.famfs/famfs.conf     - per-user config
 *   3. <shadow_path>/config    - per-mount config (highest priority)
 */
#define FAMFS_SYSTEM_CONFIG_PATH  "/etc/famfs.conf"
#define FAMFS_USER_CONFIG_DIR     ".famfs"
#define FAMFS_CONFIG_FILENAME     "famfs.conf"
#define FAMFS_LOCAL_CONFIG_NAME   "config"

/*
 * Config file format (YAML):
 *
 * interleave:
 *   nbuckets: 8
 *   nstrips: 6
 *   chunk_size: 2M
 *
 * core:
 *   verbose: 0
 *   threadct: 4
 */

/* Configuration scopes (in priority order, lowest to highest) */
enum famfs_config_scope {
	FAMFS_CONFIG_SCOPE_SYSTEM = 0,  /* /etc/famfs.conf */
	FAMFS_CONFIG_SCOPE_USER,        /* ~/.famfs/famfs.conf */
	FAMFS_CONFIG_SCOPE_LOCAL,       /* <shadow_path>/config */
	FAMFS_CONFIG_SCOPE_COUNT
};

/* Loaded configuration structure */
struct famfs_config {
	/* interleave section */
	u64 nbuckets;
	bool nbuckets_set;

	u64 nstrips;
	bool nstrips_set;

	u64 chunk_size;
	bool chunk_size_set;

	/* core section */
	int verbose;
	bool verbose_set;

	int threadct;
	bool threadct_set;
};

/*
 * Initialize config structure with defaults
 */
void famfs_config_init(struct famfs_config *cfg);

/*
 * Load config from all scopes (system -> user -> local)
 * Lower priority loaded first, higher priority overrides
 *
 * @cfg:         Output config structure
 * @shadow_path: Shadow path for local config (can be NULL to skip local)
 * @verbose:     Verbosity level for debug output
 * Returns: 0 on success, negative on error
 */
int famfs_config_load(struct famfs_config *cfg, const char *shadow_path,
		      int verbose);

/*
 * Load config from a single file
 *
 * @cfg:      Config structure (values merged/overwritten)
 * @filepath: Path to config file
 * @verbose:  Verbosity level
 * Returns: 0 on success, 1 if file doesn't exist, negative on error
 */
int famfs_config_load_file(struct famfs_config *cfg, const char *filepath,
			   int verbose);

/*
 * Get config file path for a scope
 *
 * @scope:       Config scope
 * @shadow_path: Shadow path (required for LOCAL scope)
 * @path_out:    Output buffer
 * @path_size:   Buffer size
 * Returns: 0 on success, negative on error
 */
int famfs_config_get_path(enum famfs_config_scope scope,
			  const char *shadow_path,
			  char *path_out, size_t path_size);

/*
 * Apply config to interleave_param structure
 * Only sets values that were explicitly configured
 *
 * @cfg: Loaded configuration
 * @ip:  Interleave param to update
 */
void famfs_config_apply_interleave(const struct famfs_config *cfg,
				   struct famfs_interleave_param *ip);

/*
 * Get a config value as string
 *
 * @key:         Config key (e.g., "interleave.nbuckets")
 * @scope:       Scope to read from (-1 for merged result)
 * @shadow_path: Shadow path (for LOCAL scope)
 * @value_out:   Output buffer
 * @value_size:  Buffer size
 * Returns: 0 on success, 1 if not found, negative on error
 */
int famfs_config_get(const char *key, int scope, const char *shadow_path,
		     char *value_out, size_t value_size);

/*
 * Set a config value
 *
 * @key:         Config key
 * @value:       Value to set
 * @scope:       Config scope to write to
 * @shadow_path: Shadow path (for LOCAL scope)
 * Returns: 0 on success, negative on error
 */
int famfs_config_set(const char *key, const char *value,
		     enum famfs_config_scope scope, const char *shadow_path);

/*
 * Unset (remove) a config value
 *
 * @key:         Config key
 * @scope:       Config scope
 * @shadow_path: Shadow path (for LOCAL scope)
 * Returns: 0 on success, negative on error
 */
int famfs_config_unset(const char *key, enum famfs_config_scope scope,
		       const char *shadow_path);

/*
 * List all config values
 *
 * @scope:       Scope (-1 for all merged)
 * @shadow_path: Shadow path (for LOCAL scope)
 * @show_origin: Show source file for each value
 * @fp:          Output file (e.g., stdout)
 * Returns: 0 on success, negative on error
 */
int famfs_config_list(int scope, const char *shadow_path, bool show_origin,
		      FILE *fp);

/*
 * Get scope name as string
 */
const char *famfs_config_scope_name(enum famfs_config_scope scope);

#endif /* _H_FAMFS_CONFIG */
