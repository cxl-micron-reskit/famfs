// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2025 Micron Technology, Inc.  All rights reserved.
 */

#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "famfs_meta.h"
#include "famfs_lib.h"

#define ASSERT_NE_GOTO(rc, val, bad_target) {	\
	if (rc == val) {			\
	    line = __LINE__;			\
	    goto bad_target;			\
	}					\
}

/**
 * __famfs_emit_yaml_simple_ext_list()
 *
 * Dump the bulk of the file metadata. Calls a helper for the extent list
 *
 * @emitter:  libyaml emitter struct
 * @event:    libyaml event structure
 * @fm:       famfs_log_file_meta struct
 */
static int
__famfs_emit_yaml_simple_ext_list(
	yaml_emitter_t               *emitter,
	yaml_event_t                 *event,
	const struct famfs_simple_extent   *se,
	int                           nextents)
{
	char strbuf[160];
	int i, rc;
	int line;

	/* The extent list */
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)"simple_ext_list",
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
	for (i = 0; i < nextents; i++) {
		/* YAML_MAPPING_START_EVENT: Start of extent */
		rc = yaml_mapping_start_event_initialize(event, NULL, NULL, 1,
						     YAML_BLOCK_MAPPING_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		/* Devindex */
		rc = yaml_scalar_event_initialize(event, NULL, NULL,
						  (yaml_char_t *)"devindex",
						  -1, 1, 1,
						  YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		sprintf(strbuf, "%lld", se[i].se_devindex);
		rc = yaml_scalar_event_initialize(event, NULL, NULL,
						  (yaml_char_t *)strbuf,
						  -1, 1, 1,
						  YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);


		/* Offset */
		rc = yaml_scalar_event_initialize(event, NULL, NULL,
						  (yaml_char_t *)"offset",
						  -1, 1, 1,
						  YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		sprintf(strbuf, "0x%llx", se[i].se_offset);
		rc = yaml_scalar_event_initialize(event, NULL, NULL,
						  (yaml_char_t *)strbuf,
						  -1, 1, 1,
						  YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		/* Length */
		rc = yaml_scalar_event_initialize(event, NULL, NULL,
						  (yaml_char_t *)"length",
						  -1, 1, 1,
						  YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		sprintf(strbuf, "0x%llx", se[i].se_len);
		rc = yaml_scalar_event_initialize(event, NULL, NULL,
						  (yaml_char_t *)strbuf,
						  -1, 1, 1,
						  YAML_PLAIN_SCALAR_STYLE);
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

	return 0;
err_out:
	fprintf(stderr, "%s: fail line %d rc %d errno %d problem (%s)\n",
		__func__, line, rc, errno, emitter->problem);
	perror("");

	return -1;
}

static int
__famfs_emit_yaml_striped_ext_list(
	yaml_emitter_t               *emitter,
	yaml_event_t                 *event,
	const struct famfs_log_fmap  *fmap)
{
	char strbuf[160];
	u32 i;
	int rc;
	int line;

	/* The extent list */
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)"striped_ext_list",
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
	for (i = 0; i < fmap->fmap_niext; i++) {
		/* YAML_MAPPING_START_EVENT: Start of extent */
		rc = yaml_mapping_start_event_initialize(event, NULL, NULL, 1,
						   YAML_BLOCK_MAPPING_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		/* nstrips */
		rc = yaml_scalar_event_initialize(event, NULL, NULL,
						  (yaml_char_t *)"nstrips",
						  -1, 1, 1,
						  YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		sprintf(strbuf, "%lld", fmap->ie[i].ie_nstrips);
		rc = yaml_scalar_event_initialize(event, NULL, NULL,
						  (yaml_char_t *)strbuf,
						  -1, 1, 1,
						  YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		/* chunk_size */
		rc = yaml_scalar_event_initialize(event, NULL, NULL,
						  (yaml_char_t *)"chunk_size",
						  -1, 1, 1,
						  YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		sprintf(strbuf, "0x%llx", fmap->ie[i].ie_chunk_size);
		rc = yaml_scalar_event_initialize(event, NULL, NULL,
						  (yaml_char_t *)strbuf,
						  -1, 1, 1,
						  YAML_PLAIN_SCALAR_STYLE);
		ASSERT_NE_GOTO(rc, 0, err_out);
		rc = yaml_emitter_emit(emitter, event);
		ASSERT_NE_GOTO(rc, 0, err_out);

		__famfs_emit_yaml_simple_ext_list(emitter, event,
						  fmap->ie[i].ie_strips,
						  fmap->ie[i].ie_nstrips);

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

	return 0;
err_out:
	fprintf(stderr, "%s: fail line %d rc %d errno %d problem (%s)\n",
		__func__, line, rc, errno, emitter->problem);
	perror("");

	return -1;
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
static int
__famfs_emit_yaml_file_section(
	yaml_emitter_t               *emitter,
	yaml_event_t                 *event,
	const struct famfs_log_file_meta *fm)
{
	char strbuf[160];
	int rc;
	int line;

	/* Relative path */
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)"path",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)fm->fm_relpath,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* size */
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)"size",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	sprintf(strbuf, "%lld", fm->fm_size);
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)strbuf,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* flags */
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)"flags",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	sprintf(strbuf, "%d", fm->fm_flags);
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)strbuf,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* mode */
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)"mode",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	sprintf(strbuf, "0%o", (fm->fm_mode /* | 0100000 */));
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)strbuf,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* uid */
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)"uid",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	sprintf(strbuf, "%d", fm->fm_uid);
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)strbuf,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* gid */
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)"gid",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	sprintf(strbuf, "%d", fm->fm_gid);
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)strbuf,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* nextents */
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)"nextents",
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);
	sprintf(strbuf, "%d", fm->fm_fmap.fmap_nextents);
	rc = yaml_scalar_event_initialize(event, NULL, NULL,
					  (yaml_char_t *)strbuf,
					  -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(emitter, event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* Drop in the extent list */
	switch (fm->fm_fmap.fmap_ext_type) {
	case FAMFS_EXT_SIMPLE:
		return __famfs_emit_yaml_simple_ext_list(emitter, event,
				    fm->fm_fmap.se, fm->fm_fmap.fmap_nextents);
		break;
	case FAMFS_EXT_INTERLEAVE:
		return __famfs_emit_yaml_striped_ext_list(emitter, event,
							  &fm->fm_fmap);
		break;
		
	default:
		fprintf(stderr, "%s: invalid ext type %d\n", __func__,
			fm->fm_fmap.fmap_ext_type);
	}

	return 0;
err_out:
	fprintf(stderr, "%s: fail line %d rc %d errno %d problem (%s)\n",
		__func__, line, rc, errno, emitter->problem);
	perror("");
	assert(0);

	return -1;
}

/**
 * famfs_emit_file_yaml()
 *
 * @fm:    famfs_log_file_meta structure
 * @outp:  FILE stream structure for output
 */
int
famfs_emit_file_yaml(
	const struct famfs_log_file_meta *fm,
	FILE *outp)
{
	yaml_emitter_t emitter;
	yaml_event_t event;
	int line = 0;
	int rc = 0;

	if (!yaml_emitter_initialize(&emitter)) {
		fprintf(stderr, "Failed to initialize emitter\n");
		return -1;
	}

	yaml_emitter_set_output_file(&emitter, outp);

	/* Start stream */
	if (!yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING)) {
		fprintf(stderr, "yaml_stream_start_event_initialize() failed\n");
		goto err_out;
	}
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* Start Document */
	rc = yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 0);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* YAML_MAPPING_START_EVENT:  Start Mapping */
	rc = yaml_mapping_start_event_initialize(&event, NULL, NULL, 1,
						 YAML_BLOCK_MAPPING_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* Key: file */
	rc = yaml_scalar_event_initialize(&event, NULL, NULL,
					  (yaml_char_t *)"file",
				     -1, 1, 1, YAML_PLAIN_SCALAR_STYLE);
	ASSERT_NE_GOTO(rc, 0, err_out);
	rc = yaml_emitter_emit(&emitter, &event);
	ASSERT_NE_GOTO(rc, 0, err_out);

	/* Start Mapping for file */
	rc = yaml_mapping_start_event_initialize(&event, NULL, NULL, 1,
						 YAML_BLOCK_MAPPING_STYLE);
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

	yaml_emitter_delete(&emitter);
	return 0;
err_out:
	fprintf(stderr, "%s: fail line %d rc %d errno %d problem (%s)\n",
		__func__, line, rc, errno, emitter.problem);
	perror("");

	yaml_emitter_delete(&emitter);
	return -1;
}

/* Read back in */

const char *
yaml_event_str(int event_type)
{
	switch (event_type) {
	case YAML_NO_EVENT:
		return "YAML_NO_EVENT";
	case YAML_STREAM_START_EVENT:
		return "YAML_STREAM_START_EVENT";
	case YAML_STREAM_END_EVENT:
		return "YAML_STREAM_END_EVENT";
	case YAML_DOCUMENT_START_EVENT:
		return "YAML_DOCUMENT_START_EVENT";
	case YAML_DOCUMENT_END_EVENT:
		return "YAML_DOCUMENT_END_EVENT";
	case YAML_ALIAS_EVENT:
		return "YAML_ALIAS_EVENT";
	case YAML_SCALAR_EVENT:
		return "YAML_SCALAR_EVENT";
	case YAML_SEQUENCE_START_EVENT:
		return "YAML_SEQUENCE_START_EVENT";
	case YAML_SEQUENCE_END_EVENT:
		return "YAML_SEQUENCE_END_EVENT";
	case YAML_MAPPING_START_EVENT:
		return "YAML_MAPPING_START_EVENT";
	case YAML_MAPPING_END_EVENT:
		return "YAML_MAPPING_END_EVENT";
	}
	return "BAD EVENT TYPE";
}

/* Get the next yaml event. If its type != ev_type, set rc=-1 and goto bad_target */
#define GET_YAML_EVENT_OR_GOTO(PARSER, EV, ev_type, rc, bad_target, verbose) {	\
	if (!yaml_parser_parse(PARSER, EV)) {			\
		fprintf(stderr, "%s:%d yaml parser error\n",		\
			__func__, __LINE__);				\
		rc = -1;						\
		goto bad_target;					\
	}								\
	if ((EV)->type != ev_type) {					\
		fprintf(stderr,						\
			"%s:%d: expected event type: %s but found %s\n", \
			__func__, __LINE__,				\
			yaml_event_str(ev_type),			\
			yaml_event_str((EV)->type));			\
		rc = -1;						\
		yaml_event_delete(EV);					\
		goto bad_target;					\
	} else if (verbose > 1)						\
		printf("%s: %s (%s)\n",					\
		       __func__, yaml_event_str((EV)->type),		\
		       (ev_type == YAML_SCALAR_EVENT) ?			\
		       (char *)((EV)->data.scalar.value) : "");		\
	}

#define GET_YAML_EVENT(PARSER, EV, rc, bad_target, verbose) {		\
	if (!yaml_parser_parse(PARSER, EV)) {			\
		fprintf(stderr, "%s:%d yaml parser error\n",		\
			__func__, __LINE__);				\
		rc = -1;						\
		goto bad_target;					\
	} else if (verbose > 1)						\
		printf("%s: %s (%s)\n", __func__, yaml_event_str((EV)->type), \
		       ((EV)->type == YAML_SCALAR_EVENT) ?		\
		       (char *)((EV)->data.scalar.value) : "");		\
	}

static int
famfs_parse_file_simple_ext_list(
	yaml_parser_t *parser,
	struct famfs_simple_extent *se,
	int max_extents,
	int *nparsed_out,
	int verbose)
{
	yaml_event_t event;
	int ext_index = 0;
	int done = 0;
	int rc = 0;
	int type;
	int got_ofs = 0; /* track whether we got ofs and len for each extent */
	int got_len = 0;

	/* "simple_ext_list" stanza starts wtiha  YAML_SEQUENCE_START_EVENT */
	GET_YAML_EVENT_OR_GOTO(parser, &event, YAML_SEQUENCE_START_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	/* "simple_ext_list" stanza starts wtiha  YAML_MAPPING_START_EVENT */
	GET_YAML_EVENT_OR_GOTO(parser, &event, YAML_MAPPING_START_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	while (!done) {
		yaml_event_t val_event;
#define MAX_KEY 80
		char current_key[MAX_KEY];

		GET_YAML_EVENT(parser, &event, rc, err_out, verbose);
		type = event.type;

		if (type == YAML_SCALAR_EVENT)
			strncpy(current_key, (char *)event.data.scalar.value,
				MAX_KEY - 1);

		yaml_event_delete(&event);

		switch (type) {
		case YAML_SCALAR_EVENT:

			/* Note: this assumes that the offset always comes before the
			 * length in an extent list entry */
			if (strcmp(current_key, "offset") == 0) {
				/* offset */
				got_ofs = 1;
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				se[ext_index].se_offset =
					strtoull((char *)
						 val_event.data.scalar.value,
						 0, 0);
				yaml_event_delete(&val_event);
			} else if (strcmp(current_key, "length") == 0) {
				/* length */
				got_len = 1;
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				se[ext_index].se_len =
					strtoull((char *)
						 val_event.data.scalar.value,
						 0, 0);
				yaml_event_delete(&val_event);
			} else if (strcmp(current_key, "devindex") == 0) {
				/* devindex */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				se[ext_index].se_devindex =
					strtoull((char *)
						 val_event.data.scalar.value,
						 0, 0);
				yaml_event_delete(&val_event);

			} else {
				fprintf(stderr, "%s: Bad scalar key %s\n",
					__func__, current_key);
			}

			break;

		case YAML_MAPPING_START_EVENT:
			if (verbose > 1)
				printf("%s: extent %d is coming next\n",
				       __func__, ext_index);
			if (ext_index >= max_extents) {
				fprintf(stderr,
					"%s: too many extents! (max=%d)\n",
					__func__, max_extents);
				rc = -EOVERFLOW;
				yaml_event_delete(&event);
				goto err_out;
			}
			break;
		case YAML_MAPPING_END_EVENT:
			if (!got_len || !got_ofs) {
				fprintf(stderr,
				  "%s: offset or length missing from extent\n",
					__func__);
				rc = -EINVAL;
				goto err_out;
			}
			got_ofs = got_len = 0;
			if (verbose > 1)
				printf("%s: end of extent %d\n",
				       __func__, ext_index);
			ext_index++;
			break;
		case YAML_SEQUENCE_END_EVENT:
			if (verbose > 1)
				printf("%s: finished ext list (%d entries)\n",
				       __func__, ext_index);
			done = 1;
			break;
		default:
			if (verbose > 1)
				printf("%s: unexpected event %s\n",
				       __func__, yaml_event_str(event.type));
			break;
		}
		yaml_event_delete(&val_event);

	}
	GET_YAML_EVENT_OR_GOTO(parser, &event, YAML_MAPPING_END_EVENT,
			       rc, err_out, verbose);
err_out:
	*nparsed_out = ext_index;

	return rc;
}

static int
famfs_parse_file_striped_ext_list(
	yaml_parser_t *parser,
	struct famfs_log_file_meta *fm,
	int max_extents,
	int max_strips,
	int verbose)
{
	yaml_event_t event;
	int ext_index = 0;
	int done = 0;
	int rc = 0;
	int type;

	/* "simple_ext_list" stanza starts wtiha  YAML_SEQUENCE_START_EVENT */
	GET_YAML_EVENT_OR_GOTO(parser, &event, YAML_SEQUENCE_START_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	/* "simple_ext_list" stanza starts wtiha  YAML_MAPPING_START_EVENT */
	GET_YAML_EVENT_OR_GOTO(parser, &event, YAML_MAPPING_START_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	while (!done) {
		yaml_event_t val_event;
#define MAX_KEY 80
		char current_key[MAX_KEY];

		GET_YAML_EVENT(parser, &event, rc, err_out, verbose);
		type = event.type;

		if (type == YAML_SCALAR_EVENT)
			strncpy(current_key, (char *)event.data.scalar.value,
				MAX_KEY - 1);

		yaml_event_delete(&event);

		switch (type) {
		case YAML_SCALAR_EVENT:

			if (strcmp(current_key, "chunk_size") == 0) {
				/* chunk_size */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				fm->fm_fmap.ie[ext_index].ie_chunk_size =
					strtoull((char *)
						 val_event.data.scalar.value,
						 0, 0);
				yaml_event_delete(&val_event);
				if (verbose > 1)
					printf("%s: chunk_size: 0x%llx\n",
					       __func__,
					       fm->fm_fmap.ie[ext_index].ie_chunk_size);
			} else  if (strcmp(current_key, "nstrips") == 0) {
				/* nstrips */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				fm->fm_fmap.ie[ext_index].ie_nstrips =
				    strtoull((char *)val_event.data.scalar.value,
					     0, 0);
				yaml_event_delete(&val_event);
				if (verbose > 1)
					printf("%s: nstrips: 0x%llx\n",
					       __func__,
					  fm->fm_fmap.ie[ext_index].ie_nstrips);
			} else  if (strcmp(current_key,
					   "simple_ext_list") == 0) {
				int nparsed = 0;
				/* strips (list) */

				/* The strip extent list is the same as a
				 * simple extent list, but with the max_strips
				 * limit instead of max_extents... */
				rc = famfs_parse_file_simple_ext_list(parser,
					      fm->fm_fmap.ie[ext_index].ie_strips,
					      max_strips, &nparsed, verbose);

				if (rc) {
					fprintf(stderr,
						"%s: error parsing simple ext list\n",
						__func__);
					goto err_out;
				}

				if ((u64)nparsed !=
				    fm->fm_fmap.ie[ext_index].ie_nstrips) {
					fprintf(stderr,
						"%s: chunk_size=0x%llx idx=%d\n",
						__func__,
						fm->fm_fmap.ie[ext_index].ie_chunk_size,
						ext_index);
					fprintf(stderr,
					    "%s: expect %lld strips, found %d\n",
						__func__,
						fm->fm_fmap.ie[ext_index].ie_nstrips,
						nparsed);
					rc = -EINVAL;
					goto err_out;
				}

				/* After we get the strip extents,
				 * bump the ext_index */
				ext_index++;
			} else {
				fprintf(stderr, "%s: Bad scalar key %s\n",
					__func__, current_key);
			}

			break;

		case YAML_MAPPING_START_EVENT:
			if (verbose > 1)
				printf("%s: extent %d is coming next\n",
				       __func__, ext_index);
			if (ext_index >= max_strips) {
				fprintf(stderr,
					"%s: too many extents! (max=%d)\n",
					__func__, max_extents);
				rc = -EOVERFLOW;
				yaml_event_delete(&event);
				goto err_out;
			}
			break;
		case YAML_MAPPING_END_EVENT:
			if (verbose > 1)
				printf("%s: end of extent %d\n",
				       __func__, ext_index);
			ext_index++;
			break;
		case YAML_SEQUENCE_END_EVENT:
			if (verbose > 1)
				printf("%s: finished with ext list "
				       "(%d entries)\n",
				       __func__, ext_index);
			done = 1;
			break;
		default:
			if (verbose > 1)
				printf("%s: unexpected event %s\n",
				       __func__, yaml_event_str(event.type));
			break;
		}
		yaml_event_delete(&val_event);

	}
	GET_YAML_EVENT_OR_GOTO(parser, &event, YAML_MAPPING_END_EVENT,
			       rc, err_out, verbose);
err_out:

	return rc;
}

static int
famfs_parse_file_yaml(
	yaml_parser_t *parser,
	struct famfs_log_file_meta *fm,
	int max_extents,
	int max_strips,
	int verbose)
{
	yaml_event_t event;
	int done = 0;
	char *current_key = NULL;
	int rc = 0;

	/* "file" stanza starts wtiha  YAML_MAPPING_START_EVENT */
	GET_YAML_EVENT_OR_GOTO(parser, &event, YAML_MAPPING_START_EVENT,
			       rc, err_out, verbose);

	while (!done) {
		yaml_event_t val_event;

		GET_YAML_EVENT(parser, &event, rc, err_out, verbose);

		switch (event.type) {
		case YAML_SCALAR_EVENT:
			current_key = (char *)event.data.scalar.value;

			if (strcmp(current_key, "path") == 0) {
				/* path */
				/* TODO: check for overflow */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				strncpy((char *)fm->fm_relpath,
					(char *)val_event.data.scalar.value,
					FAMFS_MAX_PATHLEN - 1);
				if (verbose > 1) printf("%s: path: %s\n",
							__func__,
							fm->fm_relpath);
				yaml_event_delete(&val_event);
			} else if (strcmp(current_key, "size") == 0) {
				/* size */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				fm->fm_size = strtoull((char *)val_event.data.scalar.value,
						       0, 0);
				yaml_event_delete(&val_event);
				if (verbose > 1) printf("%s: size: 0x%llx\n",
							__func__, fm->fm_size);
			} else if (strcmp(current_key, "flags") == 0) {
				/* flags */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				fm->fm_flags = strtoull((char *)val_event.data.scalar.value,
							0, 0);
				yaml_event_delete(&val_event);
				if (verbose > 1) printf("%s: flags: 0x%x\n",
							__func__, fm->fm_flags);
			} else if (strcmp(current_key, "mode") == 0) {
				/* mode */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				fm->fm_mode = strtoull((char *)val_event.data.scalar.value,
						       0, 0);
				yaml_event_delete(&val_event);
				if (verbose > 1) printf("%s: mode: 0%o\n",
							__func__, fm->fm_mode);
			} else if (strcmp(current_key, "uid") == 0) {
				/* uid */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				fm->fm_uid = strtoull((char *)val_event.data.scalar.value,
						      0, 0);
				yaml_event_delete(&val_event);
				if (verbose > 1) printf("%s: uid: %d\n",
							__func__, fm->fm_uid);
			} else if (strcmp(current_key, "gid") == 0) {
				/* gid */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				fm->fm_gid = strtoull((char *)val_event.data.scalar.value,
						      0, 0);
				yaml_event_delete(&val_event);
				if (verbose > 1) printf("%s: gid: %d\n",
							__func__, fm->fm_gid);
			} else if (strcmp(current_key, "nextents") == 0) {
				/* nextents */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				fm->fm_fmap.fmap_nextents = strtoull((char *)val_event.data.scalar.value,
						      0, 0);
				yaml_event_delete(&val_event);
				if (verbose > 1)
					printf("%s: nextents: %d\n",
					       __func__,
					       fm->fm_fmap.fmap_nextents);
			} else if (strcmp(current_key, "simple_ext_list") == 0) {
				int nparsed;
				/* simple_ext_list */
				fm->fm_fmap.fmap_ext_type = FAMFS_EXT_SIMPLE;
				rc = famfs_parse_file_simple_ext_list(parser,
						      fm->fm_fmap.se,
						      max_extents, &nparsed,
								      verbose);
				if (rc) {
					fprintf(stderr, "%s: error parsing "
						"simple ext list\n",
						__func__);
					goto err_out;
				}
				if ((u64)nparsed != fm->fm_fmap.fmap_nextents) {
					fprintf(stderr,
					    "%s: expect %d extents, found %d\n",
					    __func__, fm->fm_fmap.fmap_nextents,
						nparsed);
					rc = -EINVAL;
					goto err_out;

				}
			} else if (strcmp(current_key,
					  "striped_ext_list") == 0) {
				/* striped_ext_list */
				fm->fm_fmap.fmap_ext_type = FAMFS_EXT_INTERLEAVE;
				rc = famfs_parse_file_striped_ext_list(parser,
							 fm, max_extents,
							 max_strips, verbose);
				if (rc)
					goto err_out;
			} else {
				fprintf(stderr,
					"%s: Unrecognized scalar key %s\n",
					__func__, current_key);
				rc = -EINVAL;
				goto err_out;
			}
			current_key = NULL;
			break;

		case YAML_MAPPING_END_EVENT:
			if (verbose > 1)
				printf("%s: Finished with file yaml\n",
				       __func__);
			done = 1;
			break;
		default:
			fprintf(stderr, "%s: unexpected libyaml event %s\n",
				__func__, yaml_event_str(event.type));
			break;
		}

		yaml_event_delete(&event);
	}

err_out:
	return rc;
}

int
famfs_parse_shadow_yaml(
	FILE *fp,
	struct famfs_log_file_meta *fm,
	int max_extents,
	int max_strips,
	int verbose)
{
	yaml_parser_t parser;
	yaml_event_t event;
	int rc = 0;

	if (verbose > 1)
		printf("\n\n%s:\n", __func__);

	if (!yaml_parser_initialize(&parser)) {
		fprintf(stderr, "Failed to initialize parser\n");
		return -1;
	}

	yaml_parser_set_input_file(&parser, fp);

	/* Look for YAML_STREAM_START_EVENT */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_STREAM_START_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	/* Look for YAML_DOCUMENT_START_EVENT */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_DOCUMENT_START_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	/* Look for YAML_MAPPING_START_EVENT */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_MAPPING_START_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	/* Look for "file" stanza as scalar event
	 * Theoretically there could be other stanzas later,
	 * but this is the only one now
	 */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_SCALAR_EVENT,
			       rc, err_out, verbose);
	if (strcmp((char *)"file", (char *)event.data.scalar.value) == 0) {
		rc = famfs_parse_file_yaml(&parser, fm, max_extents,
					   max_strips, verbose);
		if (rc) {
			yaml_event_delete(&event);
			goto err_out;
		}
	}
	yaml_event_delete(&event);

	/* Look for YAML_DOCUMENT_END_EVENT */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_DOCUMENT_END_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	/* Look for YAML_STREAM_END_EVENT */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_STREAM_END_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

err_out:
	yaml_parser_delete(&parser);
	return rc;
}

/*
 * Allocation yaml
 *
 * This is not the file yaml! This is the .meta/.alloc.cfg file!!
 *
 * This file currently contains interleaved_alloc:
 * (nbuckets, nstrips and chunk_size) and nothing else -
 * but it may be expanded later.
 */
static int
famfs_parse_stripe_config_yaml(
	yaml_parser_t *parser,
	struct famfs_interleave_param *interleave_param,
	int verbose)
{
	yaml_event_t event;
	int done = 0;
	char *current_key = NULL;
	int rc = 0;

	/* interleaved_alloc stanza starts with a  YAML_MAPPING_START_EVENT */
	GET_YAML_EVENT_OR_GOTO(parser, &event, YAML_MAPPING_START_EVENT,
			       rc, err_out, verbose);

	while (!done) {
		yaml_event_t val_event;

		GET_YAML_EVENT(parser, &event, rc, err_out, verbose);

		switch (event.type) {
		case YAML_SCALAR_EVENT:
			current_key = (char *)event.data.scalar.value;
			if (verbose > 1)
				printf("%s: current_key=%s\n", __func__,
				       current_key);

			if (strcmp(current_key, "nbuckets") == 0) {
				/* nbuckets */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				interleave_param->nbuckets = strtoull((char *)val_event.data.scalar.value,
							    0, 0);
				yaml_event_delete(&val_event);
				if (verbose > 1)
					printf("%s: nbuckets: %lld\n",
					   __func__, interleave_param->nbuckets);
			} else if (strcmp(current_key, "nstrips") == 0) {
				/* nstrips */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				interleave_param->nstrips = strtoull((char *)val_event.data.scalar.value,
							    0, 0);
				yaml_event_delete(&val_event);
				if (verbose > 1)
					printf("%s: nstrips: %lld\n",
					   __func__, interleave_param->nstrips);
			} else if (strcmp(current_key, "chunk_size") == 0) {
				char tmpstr[256];
				char *endptr;
				s64 mult;

				/* chunk_size */
				GET_YAML_EVENT_OR_GOTO(parser, &val_event,
						       YAML_SCALAR_EVENT,
						       rc, err_out, verbose);
				strncpy(tmpstr,
					(char *)val_event.data.scalar.value,
					255);

				interleave_param->chunk_size = strtoull(tmpstr,
									&endptr,
									0);
				mult = get_multiplier(endptr);
				if (mult > 0)
					interleave_param->chunk_size *= mult;

				yaml_event_delete(&val_event);
				if (verbose > 1)
					printf("%s: chunk_size: %lld\n",
					       __func__,
					       interleave_param->chunk_size);
			} else {
				fprintf(stderr,
					"%s: Unrecognized scalar key: %s\n",
					__func__, current_key);
				rc = -EINVAL;
				goto err_out;
			}
			current_key = NULL;
			break;

		case YAML_MAPPING_END_EVENT:
			if (verbose > 1)
				printf("%s: Finished with file yaml\n",
				       __func__);
			done = 1;
			break;
		default:
			fprintf(stderr, "%s: unexpected libyaml event %s\n",
				__func__, yaml_event_str(event.type));
			break;
		}

		yaml_event_delete(&event);
	}

err_out:
	return rc;
}

/**
 * famfs_parse_alloc_yaml()
 *
 * Parse the yaml config file that contains the stripe configuration
 */
int
famfs_parse_alloc_yaml(
	FILE *fp,
	struct famfs_interleave_param *stripe,
	int verbose)
{
	yaml_parser_t parser;
	yaml_event_t event;
	int rc = 0;

	if (verbose > 1)
		printf("\n\n%s: \n", __func__);

	if (!yaml_parser_initialize(&parser)) {
		fprintf(stderr, "Failed to initialize parser\n");
		return -1;
	}

	yaml_parser_set_input_file(&parser, fp);

	/* Look for YAML_STREAM_START_EVENT */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_STREAM_START_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	/* Look for YAML_DOCUMENT_START_EVENT */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_DOCUMENT_START_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	/* Look for YAML_MAPPING_START_EVENT */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_MAPPING_START_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	/* Look for "file" stanza as scalar event
	 * Theoretically there could be other stanzas later,
	 * but this is the only one now
	 */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_SCALAR_EVENT,
			       rc, err_out, verbose);
	if (strcmp((char *)"interleaved_alloc",
		   (char *)event.data.scalar.value) == 0) {
		rc = famfs_parse_stripe_config_yaml(&parser, stripe, verbose);
		if (rc) {
			yaml_event_delete(&event);
			goto err_out;
		}
	}
	yaml_event_delete(&event);

	/* Look for YAML_MAPPING_END_EVENT - end of interleaved_alloc stanza */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_MAPPING_END_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	/* Look for YAML_DOCUMENT_END_EVENT */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_DOCUMENT_END_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

	/* Look for YAML_STREAM_END_EVENT */
	GET_YAML_EVENT_OR_GOTO(&parser, &event, YAML_STREAM_END_EVENT,
			       rc, err_out, verbose);
	yaml_event_delete(&event);

err_out:
	yaml_parser_delete(&parser);
	return rc;
}
