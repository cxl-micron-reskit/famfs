// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2024 Micron Technology, Inc.  All rights reserved.
 */

#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "famfs_meta.h"

#define ASSERT_NE_GOTO(rc, val, bad_target) {	\
	if (rc == val) {			\
	    line = __LINE__;			\
	    err = 1;				\
	    goto bad_target;			\
	}					\
}

/**
 * __famfs_emit_yaml_ext_list()
 *
 * Dump the bulk of the file metadata. Calls a helper for the extent list
 *
 * @emitter:  libyaml emitter struct
 * @event:    libyaml event structure
 * @fm:       famfs_file_meta struct
 */
int
__famfs_emit_yaml_ext_list(
	yaml_emitter_t               *emitter,
	yaml_event_t                 *event,
	const struct famfs_file_meta *fm)
{
	char strbuf[160];
	int i, rc;
	int line;
	int err = 0;

	/* The extent list */
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)"simple_ext_list",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* Start event for the sequence of extents */
	rc = yaml_sequence_start_event_initialize(event, NULL, NULL,
						  1, YAML_BLOCK_SEQUENCE_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* The extents */
	for (i = 0; i < fm->fm_nextents; i++) {
		/* YAML_MAPPING_START_EVENT: Start of extent */
		rc = yaml_mapping_start_event_initialize(event, NULL, NULL, 1,
							 YAML_BLOCK_MAPPING_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		/* Offset */
		rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)"offset",
						  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		sprintf(strbuf, "0x%llx", fm->fm_ext_list[i].se.se_offset);
		rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)strbuf,
						  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		/* Length */
		rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)"length",
						  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		sprintf(strbuf, "0x%llx", fm->fm_ext_list[i].se.se_len);
		rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)strbuf,
						  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		/* YAML_MAPPING_END_EVENT: End of extent */
		rc = yaml_mapping_end_event_initialize(event);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);
	}

	/* End event for the sequence of events */
	rc = yaml_sequence_end_event_initialize(event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

err_out:
	if (err) {
		fprintf(stderr, "%s: fail line %d rc %d errno %d problem (%s)\n",
			__func__, line, rc, errno, emitter->problem);
		perror("");
		assert(0);
	}
	return 1;
}


/**
 * __famfs_emit_yaml_file_section()
 *
 * Dump the bulk of the file metadata. Calls a helper for the extent list
 *
 * @emitter:  libyaml emitter struct
 * @event:    libyaml event structure
 * @fm:       famfs_file_meta struct
 */
int
__famfs_emit_yaml_file_section(
	yaml_emitter_t               *emitter,
	yaml_event_t                 *event,
	const struct famfs_file_meta *fm)
{
	char strbuf[160];
	int rc;
	int line;
	int err = 0;

	/* Relative path */
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)"path",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)fm->fm_relpath,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* size */
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)"size",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	sprintf(strbuf, "%lld", fm->fm_size);
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)strbuf,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* flags */
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)"flags",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	sprintf(strbuf, "%d", fm->fm_flags);
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)strbuf,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* mode */
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)"mode",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	sprintf(strbuf, "0%o", fm->fm_mode);
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)strbuf,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* uid */
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)"uid",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	sprintf(strbuf, "%d", fm->fm_uid);
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)strbuf,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* gid */
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)"gid",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	sprintf(strbuf, "%d", fm->fm_gid);
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)strbuf,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* nextents */
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)"nextents",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	sprintf(strbuf, "%d", fm->fm_nextents);
	rc = yaml_scalar_event_initialize(event, NULL, NULL, (yaml_char_t *)strbuf,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* Drop in the extent list */
	__famfs_emit_yaml_ext_list(emitter, event, fm);

err_out:
	if (err) {
		fprintf(stderr, "%s: fail line %d rc %d errno %d problem (%s)\n",
			__func__, line, rc, errno, emitter->problem);
		perror("");
		assert(0);
	}
	return 1;
}

/**
 * famfs_emit_file_yaml()
 *
 * @fm:    famfs_file_meta structure
 * @outp:  FILE stream structure for output
 */
int
famfs_emit_file_yaml(
	const struct famfs_file_meta *fm,
	FILE *outp)
{
	yaml_emitter_t emitter;
	yaml_event_t event;
	//char strbuf[160];
	int err = 0;
	int line;
	int rc;
	//int i;

	if (!yaml_emitter_initialize(&emitter)) {
		fprintf(stderr, "Failed to initialize emitter\n");
		return -1;
	}

	yaml_emitter_set_output_file(&emitter, outp);

	// Start Stream
	if (!yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING)) {
		fprintf(stderr, "yaml_stream_start_event_initialize() failed\n");
		goto err_out;
	}
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	// Start Document
	rc = yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 0);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* YAML_MAPPING_START_EVENT:  Start Mapping */
	rc = yaml_mapping_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	// Key: file
	rc = yaml_scalar_event_initialize(&event, NULL, NULL, (yaml_char_t *)"file",
				     -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	// Start Mapping for file
	rc = yaml_mapping_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out);


	__famfs_emit_yaml_file_section(&emitter, &event, fm);

	/* End for section indented under "file:" */
	rc = yaml_mapping_end_event_initialize(&event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out); /* boom */

	/* End Mapping */
	rc = yaml_mapping_end_event_initialize(&event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* End Document */
	rc = yaml_document_end_event_initialize(&event, 0);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* End Stream */
	rc = yaml_stream_end_event_initialize(&event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out);

err_out:
	if (err) {
		fprintf(stderr, "%s: fail line %d rc %d errno %d problem (%s)\n",
			__func__, line, rc, errno, emitter.problem);
		perror("");
		return -1;
	}
	yaml_emitter_delete(&emitter);
	return 0;
}

/* Read back in */

#if 0
int
famfs_parse_file_yaml(
	FILE *fp,
	struct famfs_file_meta *fm,
	int max_extents)
{
	yaml_parser_t parser;
	yaml_event_t event;
	int done = 0;
	int state = 0; /* 0: expecting key, 1: expecting value, 2: expecting extent fields */
	int ext_num = 0;
	char *current_key = NULL;


	if (!yaml_parser_initialize(&parser)) {
		fprintf(stderr, "Failed to initialize parser\n");
		return -ENOMEM;
	}

	yaml_parser_set_input_file(&parser, fp);

	while (!done) {
		if (!yaml_parser_parse(&parser, &event)) {
			fprintf(stderr, "Parser error %d\n", parser.error);
			break;
		}

		switch (event.type) {
		case YAML_STREAM_END_EVENT:
			done = 1;
			break;
		case YAML_SCALAR_EVENT:
			if (state == 0) {
				current_key = strdup((char *)event.data.scalar.value);
				state = 1;
			} else if (state == 1) {
				if (strcmp(current_key, "path") == 0) {
					strncpy((char *)fm->fm_relpath,
						(char *)event.data.scalar.value,
						FAMFS_MAX_PATHLEN);
				} else if (strcmp(current_key, "size") == 0) {
					fm->fm_size = strtoull((char *)event.data.scalar.value,
							       0, 0);
				} else if (strcmp(current_key, "flags") == 0) {
					fm->fm_flags = strtoul((char *)event.data.scalar.value,
							       0, 0);
				} else if (strcmp(current_key, "mode") == 0) {
					fm->fm_mode = strtoul((char *)event.data.scalar.value,
							      0, 8); /* must be octal */
				} else if (strcmp(current_key, "uid") == 0) {
					fm->fm_uid = strtoul((char *)event.data.scalar.value,
							     0, 0);
				} else if (strcmp(current_key, "gid") == 0) {
					fm->fm_gid = strtoul((char *)event.data.scalar.value,
							     0, 0);
				} else if (strcmp(current_key, "nextents") == 0) {
					fm->fm_nextents = strtoul((char *)event.data.scalar.value,
								  0, 0);
				} else if (strcmp(current_key, "offset") == 0) {
					assert(state == 2);
					assert(ext_num < fm->fm_nextents);
					assert(ext_num < max_extents);
					fm->fm_ext_list[ext_num].se.se_offset =
						strtoull((char *)event.data.scalar.value, 0, 0);
				} else if (strcmp(current_key, "length") == 0) {
					assert(state == 2);
					assert(ext_num < fm->fm_nextents);
					assert(ext_num < max_extents);
					fm->fm_ext_list[ext_num].se.se_len =
						strtoull((char *)event.data.scalar.value, 0, 0);
				} else {
					printf("oops: current_key: %s\n", current_key);
				}

				free(current_key);
				current_key = NULL;
				state = 0;
			}
			break;
		case YAML_MAPPING_START_EVENT:
			if (current_key && strcmp(current_key, "tags") == 0) {
				state = 2; /* Parsing list of extents */
			}
			break;
		case YAML_MAPPING_END_EVENT:
			if (state == 2) {
				ext_num++;
				state = 0; /* Finished an extent mapping */
			}
			break;
		default:
			break;
		}

		yaml_event_delete(&event);
	}

	yaml_parser_delete(&parser);
	fclose(fp);
	return 0;
}
#endif

#if 0
int main() {
    File file;
    file.name = NULL;
    file.size = 0;
    memset(file.tags, 0, sizeof(file.tags));

    parse_file_yaml(&file);

    printf("Name: %s\n", file.name);
    printf("Size: %d\n", file.size);
    printf("Tags:\n");
    for (int i = 0; i < 3; i++) {
        printf("  - Offset: %d, Length: %d\n", file.tags[i].offset, file.tags[i].length);
    }

    // Free allocated memory
    free(file.name);

    return 0;
}
#endif

/**/
