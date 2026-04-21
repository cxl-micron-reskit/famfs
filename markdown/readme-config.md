# famfs Configuration System

## Overview

A git-style hierarchical configuration system for the famfs CLI that allows users to set default values for command parameters.

---

## Why

**Problem**: Users had to repeat the same command-line options every time:
```bash
famfs cp --nbuckets 8 --nstrips 6 --chunksize 2M file1 /mnt/famfs/
famfs cp --nbuckets 8 --nstrips 6 --chunksize 2M file2 /mnt/famfs/
famfs cp --nbuckets 8 --nstrips 6 --chunksize 2M file3 /mnt/famfs/
```

**Solution**: Set once in config, use everywhere:
```bash
famfs config --global --set interleave.nbuckets 8
famfs config --global --set interleave.nstrips 6
famfs config --global --set interleave.chunk_size 2M

famfs cp file1 /mnt/famfs/   # Uses config defaults
famfs cp file2 /mnt/famfs/   # Uses config defaults
famfs cp file3 /mnt/famfs/   # Uses config defaults
```

---

## What

### New Files

| Component | Description |
|-----------|-------------|
| `src/famfs_config.h` | API header (164 lines) |
| `src/famfs_config.c` | YAML-based implementation using libyaml (787 lines) |

### Modified Files

| Component | Changes |
|-----------|---------|
| `src/famfs_cli.c` | Added `config` subcommand, integrated config loading into `cp` and `creat` |
| `CMakeLists.txt` | Added `famfs_config.c` to libfamfs |

### Config File Locations

```
/etc/famfs.conf         -> System-wide defaults (priority 1)
~/.famfs/famfs.conf     -> Per-user preferences (priority 2)
<shadow_path>/config    -> Per-mount config (priority 3, highest)
```

### Config File Format (YAML)

```yaml
interleave:
  nbuckets: 8
  nstrips: 6
  chunk_size: 2M

core:
  verbose: 0
  threadct: 4
```

### Configuration Keys

| Key | Description |
|-----|-------------|
| `interleave.nbuckets` | Number of buckets for interleaved allocation |
| `interleave.nstrips` | Number of strips for interleaved allocation |
| `interleave.chunk_size` | Chunk size (supports K, M, G suffixes) |
| `core.verbose` | Default verbosity level |
| `core.threadct` | Default thread count for multi-file operations |

---

## How

### Flow Diagram - Config Loading

```
+-----------------------+     +-----------------------+     +-----------------------+
| /etc/famfs.conf       |     | ~/.famfs/famfs.conf   |     | <shadow_path>/config  |
| (system)              |     | (user)                |     | (local, per-mount)    |
|                       |     |                       |     |                       |
| interleave:           |     | interleave:           |     | interleave:           |
|   nbuckets: 4         |     |   nbuckets: 8  <-------- overrides |   nstrips: 12  <-------- overrides
|   nstrips: 4          |     |                       |     |                       |
+-----------+-----------+     +-----------+-----------+     +-----------+-----------+
            | load first                  | load second               | load third
            |                             |                           |
            +-------------+---------------+---------------------------+
                          |
                          v
              +-----------------------+
              |  famfs_config_load()  |
              |                       |
              |  Merged result:       |
              |    nbuckets = 8       |
              |    nstrips  = 12      |
              +-----------+-----------+
                          |
                          v
              +-----------------------+
              |  CLI Command (cp)     |
              |                       |
              |  --nstrips 16  <-------- cmdline overrides config
              |                       |
              |  Final:               |
              |    nbuckets = 8       |
              |    nstrips  = 16      |
              +-----------------------+
```

### Priority Chain (lowest to highest)

```
+----------+    +----------+    +----------+    +----------+    +----------+
| Built-in | -> | /etc/    | -> | ~/.famfs/| -> | shadow   | -> | Command  |
| Defaults |    | famfs.   |    | famfs.   |    | path/    |    | Line     |
|          |    | conf     |    | conf     |    | config   |    |          |
+----------+    +----------+    +----------+    +----------+    +----------+
   lowest                                                          highest
```

### Flow Diagram - Command Execution

```
$ famfs cp --nstrips 16 src.dat /mnt/famfs/

         |
         v
+-------------------------------------+
|  1. Parse command line (getopt)     |
|     - Save --nstrips 16             |
+-----------------+-------------------+
                  |
                  v
+-------------------------------------+
|  2. Get shadow_path from dest       |
|     - Read xattr from /mnt/famfs/   |
|     - Gets /run/famfs/.meta         |
+-----------------+-------------------+
                  |
                  v
+-------------------------------------+
|  3. famfs_config_load(&cfg)         |
|     - Read /etc/famfs.conf          |
|     - Read ~/.famfs/famfs.conf      |
|     - Read <shadow_path>/config     |
|     - Merge values                  |
+-----------------+-------------------+
                  |
                  v
+-------------------------------------+
|  4. famfs_config_apply_interleave() |
|     - Apply config to defaults      |
|     - Cmdline overrides config      |
+-----------------+-------------------+
                  |
                  v
+-------------------------------------+
|  5. Execute with merged params      |
|     famfs_cp_multi(...)             |
+-------------------------------------+
```

---

## Libraries & Licenses

### Dependencies

| Library | Purpose | License | Notes |
|---------|---------|---------|-------|
| **libyaml** | YAML parsing | MIT | Already a famfs dependency |

### Why libyaml?

- **No new dependencies** - famfs already uses libyaml for shadow file metadata
- **MIT License** - Permissive, no copyleft restrictions
- **Consistent** - Same parsing patterns as existing `famfs_yaml.c`

### Alternatives Considered

| Library | License | Why Not Used |
|---------|---------|--------------|
| libconfig | LGPL | Copyleft restrictions |
| inih | BSD | Would add new dependency |
| Custom INI parser | - | Reinventing the wheel |

### License Compatibility

```
famfs (Apache-2.0) + libyaml (MIT) = Compatible
```

The config patch itself is licensed under **Apache-2.0** (same as famfs).

---

## CLI Usage

### List Configuration

```bash
# List all config values
famfs config --list

# List with source file shown
famfs config --list --show-origin

# List including per-mount config
famfs config --list --shadow-path /run/famfs/.meta
```

### Get a Value

```bash
famfs config --get interleave.nbuckets
famfs config --get interleave.nstrips
famfs config --get core.threadct

# Get with per-mount config included
famfs config --get interleave.nbuckets --shadow-path /run/famfs/.meta
```

### Set Values

```bash
# Set in user config (~/.famfs/famfs.conf)
famfs config --global --set interleave.nbuckets 8
famfs config --global --set interleave.nstrips 6
famfs config --global --set interleave.chunk_size 2M

# Set in system config (/etc/famfs.conf) - requires root
sudo famfs config --system --set interleave.nbuckets 4

# Set in per-mount config (<shadow_path>/config)
famfs config --local --shadow-path /run/famfs/.meta --set interleave.nstrips 12
```

### Unset Values

```bash
famfs config --global --unset interleave.nbuckets
famfs config --local --shadow-path /run/famfs/.meta --unset interleave.nstrips
```

---

## Key Design Points

1. **Stateless**: CLI reads config fresh on every invocation (like git)
2. **No new dependencies**: Uses existing libyaml (MIT licensed)
3. **Non-breaking**: Commands work without config files
4. **Overridable**: Command-line always wins
5. **License-clean**: All components permissively licensed
6. **Consistent**: Uses same YAML patterns as existing famfs code

---

## API Reference

### Core Functions

```c
/* Initialize config structure */
void famfs_config_init(struct famfs_config *cfg);

/* Load config from all scopes (system -> user -> local) */
int famfs_config_load(struct famfs_config *cfg, const char *shadow_path, int verbose);

/* Load config from a single file */
int famfs_config_load_file(struct famfs_config *cfg, const char *filepath, int verbose);

/* Apply config to interleave_param structure */
void famfs_config_apply_interleave(const struct famfs_config *cfg,
                                   struct famfs_interleave_param *ip);

/* Get config file path for a scope */
int famfs_config_get_path(enum famfs_config_scope scope,
                          const char *shadow_path,
                          char *path_out, size_t path_size);
```

### CLI Functions

```c
/* Get a config value as string */
int famfs_config_get(const char *key, int scope, const char *shadow_path,
                     char *value_out, size_t value_size);

/* Set a config value */
int famfs_config_set(const char *key, const char *value,
                     enum famfs_config_scope scope, const char *shadow_path);

/* Unset a config value */
int famfs_config_unset(const char *key, enum famfs_config_scope scope,
                       const char *shadow_path);

/* List all config values */
int famfs_config_list(int scope, const char *shadow_path, bool show_origin, FILE *fp);
```

### Configuration Scopes

```c
enum famfs_config_scope {
    FAMFS_CONFIG_SCOPE_SYSTEM = 0,  /* /etc/famfs.conf */
    FAMFS_CONFIG_SCOPE_USER,        /* ~/.famfs/famfs.conf */
    FAMFS_CONFIG_SCOPE_LOCAL,       /* <shadow_path>/config */
    FAMFS_CONFIG_SCOPE_COUNT
};
```

---

## Example: Typical Setup

```bash
# 1. System admin sets site-wide defaults
sudo famfs config --system --set interleave.nbuckets 4
sudo famfs config --system --set core.threadct 8

# 2. User overrides for their preference
famfs config --global --set interleave.nbuckets 8
famfs config --global --set interleave.nstrips 6

# 3. Set per-mount defaults for specific famfs instance
famfs config --local --shadow-path /run/famfs/.meta --set interleave.nstrips 12

# 4. Verify configuration
famfs config --list --show-origin
famfs config --list --show-origin --shadow-path /run/famfs/.meta

# 5. Use famfs commands (config applied automatically)
famfs cp large_dataset.bin /mnt/famfs/
famfs creat -s 1G /mnt/famfs/newfile.dat

# 6. Override config for one-off command
famfs cp --nstrips 16 special_file.bin /mnt/famfs/
```
