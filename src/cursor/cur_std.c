/*-
 * Copyright (c) 2008-2012 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

/*
 * __wt_cursor_notsup --
 *	Unsupported cursor actions.
 */
int
__wt_cursor_notsup(WT_CURSOR *cursor)
{
	WT_UNUSED(cursor);

	return (ENOTSUP);
}

/*
 * __wt_cursor_get_key --
 *	WT_CURSOR->get_key default implementation.
 */
int
__wt_cursor_get_key(WT_CURSOR *cursor, ...)
{
	WT_DECL_RET;
	va_list ap;

	va_start(ap, cursor);
	ret = __wt_cursor_get_keyv(cursor, cursor->flags, ap);
	va_end(ap);
	return (ret);
}

/*
 * __wt_cursor_get_keyv --
 *	WT_CURSOR->get_key worker function.
 */
int
__wt_cursor_get_keyv(WT_CURSOR *cursor, uint32_t flags, va_list ap)
{
	WT_DECL_RET;
	WT_ITEM *key;
	WT_SESSION_IMPL *session;
	size_t size;
	const char *fmt;

	CURSOR_API_CALL(cursor, session, get_key, NULL);
	WT_CURSOR_NEEDKEY(cursor);

	if (WT_CURSOR_RECNO(cursor)) {
		if (LF_ISSET(WT_CURSTD_RAW)) {
			key = va_arg(ap, WT_ITEM *);
			key->data = cursor->raw_recno_buf;
			WT_ERR(__wt_struct_size(
			    session, &size, "q", cursor->recno));
			key->size = (uint32_t)size;
			ret = __wt_struct_pack(session, cursor->raw_recno_buf,
			    sizeof(cursor->raw_recno_buf), "q", cursor->recno);
		} else
			*va_arg(ap, uint64_t *) = cursor->recno;
	} else {
		fmt = cursor->key_format;
		if (LF_ISSET(
		    WT_CURSTD_DUMP_HEX | WT_CURSTD_DUMP_PRINT | WT_CURSTD_RAW))
			fmt = "u";
		ret = __wt_struct_unpackv(
		    session, cursor->key.data, cursor->key.size, fmt, ap);
	}

err:	API_END(session);
	return (ret);
}

/*
 * __wt_cursor_get_value --
 *	WT_CURSOR->get_value default implementation.
 */
int
__wt_cursor_get_value(WT_CURSOR *cursor, ...)
{
	WT_DECL_RET;
	WT_SESSION_IMPL *session;
	const char *fmt;
	va_list ap;

	CURSOR_API_CALL(cursor, session, get_value, NULL);
	WT_CURSOR_NEEDVALUE(cursor);

	va_start(ap, cursor);
	fmt = F_ISSET(cursor,
	    WT_CURSTD_DUMP_HEX | WT_CURSTD_DUMP_PRINT | WT_CURSTD_RAW) ?
	    "u" : cursor->value_format;
	ret = __wt_struct_unpackv(session,
	    cursor->value.data, cursor->value.size, fmt, ap);
	va_end(ap);

err:	API_END(session);
	return (ret);
}

/*
 * __wt_cursor_set_keyv --
 *	WT_CURSOR->set_key default implementation.
 */
void
__wt_cursor_set_key(WT_CURSOR *cursor, ...)
{
	va_list ap;

	va_start(ap, cursor);
	__wt_cursor_set_keyv(cursor, cursor->flags, ap);
	va_end(ap);
}

/*
 * __wt_cursor_set_keyv --
 *	WT_CURSOR->set_key default implementation.
 */
void
__wt_cursor_set_keyv(WT_CURSOR *cursor, uint32_t flags, va_list ap)
{
	WT_DECL_RET;
	WT_SESSION_IMPL *session;
	WT_ITEM *buf, *item;
	size_t sz;
	va_list ap_copy;
	const char *fmt, *str;

	CURSOR_API_CALL(cursor, session, set_key, NULL);

	/* Fast path some common cases: single strings or byte arrays. */
	if (WT_CURSOR_RECNO(cursor)) {
		if (LF_ISSET(WT_CURSTD_RAW)) {
			item = va_arg(ap, WT_ITEM *);
			WT_ERR(__wt_struct_unpack(session,
			    item->data, item->size, "q", &cursor->recno));
		} else
			cursor->recno = va_arg(ap, uint64_t);
		if (cursor->recno == 0)
			WT_ERR_MSG(session, EINVAL,
			    "Record numbers must be greater than zero");
		cursor->key.data = &cursor->recno;
		sz = sizeof(cursor->recno);
	} else {
		fmt = cursor->key_format;
		if (LF_ISSET(
		    WT_CURSTD_DUMP_HEX | WT_CURSTD_DUMP_PRINT | WT_CURSTD_RAW))
			fmt = "u";
		if (strcmp(fmt, "S") == 0) {
			str = va_arg(ap, const char *);
			sz = strlen(str) + 1;
			cursor->key.data = (void *)str;
		} else if (strcmp(fmt, "u") == 0) {
			item = va_arg(ap, WT_ITEM *);
			sz = item->size;
			cursor->key.data = (void *)item->data;
		} else {
			buf = &cursor->key;

			va_copy(ap_copy, ap);
			ret = __wt_struct_sizev(
			    session, &sz, cursor->key_format, ap_copy);
			va_end(ap_copy);
			WT_ERR(ret);

			WT_ERR(__wt_buf_initsize(session, buf, sz));
			WT_ERR(__wt_struct_packv(
			    session, buf->mem, sz, cursor->key_format, ap));
		}
	}
	if (sz == 0)
		WT_ERR_MSG(session, EINVAL, "Empty keys not permitted");
	else if ((uint32_t)sz != sz)
		WT_ERR_MSG(session, EINVAL,
		    "Key size (%" PRIu64 ") out of range", (uint64_t)sz);
	cursor->saved_err = 0;
	cursor->key.size = WT_STORE_SIZE(sz);
	F_SET(cursor, WT_CURSTD_KEY_SET);
	if (0) {
err:		cursor->saved_err = ret;
		F_CLR(cursor, WT_CURSTD_KEY_SET);
	}

	API_END(session);
}

/*
 * __wt_cursor_set_value --
 *	WT_CURSOR->set_value default implementation.
 */
void
__wt_cursor_set_value(WT_CURSOR *cursor, ...)
{
	WT_DECL_RET;
	WT_ITEM *buf, *item;
	WT_SESSION_IMPL *session;
	const char *fmt, *str;
	size_t sz;
	va_list ap;

	CURSOR_API_CALL(cursor, session, set_value, NULL);

	va_start(ap, cursor);
	fmt = F_ISSET(cursor,
	    WT_CURSTD_DUMP_HEX | WT_CURSTD_DUMP_PRINT | WT_CURSTD_RAW) ?
	    "u" : cursor->value_format;
	/* Fast path some common cases: single strings or byte arrays. */
	if (strcmp(fmt, "S") == 0) {
		str = va_arg(ap, const char *);
		sz = strlen(str) + 1;
		cursor->value.data = str;
	} else if (strcmp(fmt, "u") == 0) {
		item = va_arg(ap, WT_ITEM *);
		sz = item->size;
		cursor->value.data = item->data;
	} else {
		buf = &cursor->value;
		ret = __wt_struct_sizev(session, &sz, cursor->value_format, ap);
		va_end(ap);
		WT_ERR(ret);
		va_start(ap, cursor);
		if ((ret = __wt_buf_initsize(session, buf, sz)) != 0 ||
		    (ret = __wt_struct_packv(session, buf->mem, sz,
		    cursor->value_format, ap)) != 0) {
			cursor->saved_err = ret;
			F_CLR(cursor, WT_CURSTD_VALUE_SET);
			goto err;
		}
		cursor->value.data = buf->mem;
	}
	F_SET(cursor, WT_CURSTD_VALUE_SET);
	cursor->value.size = WT_STORE_SIZE(sz);
	va_end(ap);

err:	API_END(session);
}

/*
 * __cursor_search --
 *	WT_CURSOR->search default implementation.
 */
static int
__cursor_search(WT_CURSOR *cursor)
{
	int exact;

	WT_RET(cursor->search_near(cursor, &exact));
	return ((exact == 0) ? 0 : WT_NOTFOUND);
}

/*
 * __cursor_equals --
 *	WT_CURSOR->equals default implementation.
 */
static int
__cursor_equals(WT_CURSOR *cursor, WT_CURSOR *other)
{
	WT_DECL_RET;
	WT_SESSION_IMPL *session;

	CURSOR_API_CALL(cursor, session, equals, NULL);

	/* Both cursors must refer to the same source. */
	if (other == NULL || strcmp(cursor->uri, other->uri) != 0)
		goto done;

	/* Check that both have keys set and the keys match. */
	if (F_ISSET(cursor, WT_CURSTD_KEY_SET) &&
	    F_ISSET(other, WT_CURSTD_KEY_SET)) {
		if (WT_CURSOR_RECNO(cursor))
			ret = (cursor->recno == other->recno);
		else if (cursor->key.size == other->key.size)
			ret = (memcmp(cursor->key.data, other->key.data,
			    cursor->key.size) == 0);
	}

done:	API_END(session);
	return (ret);
}

/*
 * __wt_cursor_close --
 *	WT_CURSOR->close default implementation.
 */
int
__wt_cursor_close(WT_CURSOR *cursor)
{
	WT_DECL_RET;
	WT_SESSION_IMPL *session;

	CURSOR_API_CALL(cursor, session, close, NULL);

	__wt_buf_free(session, &cursor->key);
	__wt_buf_free(session, &cursor->value);

	if (F_ISSET(cursor, WT_CURSTD_OPEN))
		TAILQ_REMOVE(&session->cursors, cursor, q);

	__wt_free(session, cursor->uri);
	__wt_free(session, cursor);

	API_END(session);
	return (ret);
}

/*
 * __wt_cursor_dup --
 *	Duplicate a cursor.
 */
int
__wt_cursor_dup(WT_SESSION_IMPL *session,
    WT_CURSOR *to_dup, const char *config, WT_CURSOR **cursorp)
{
	WT_CURSOR *cursor;
	WT_DECL_RET;
	WT_ITEM key;
	WT_SESSION *wt_session;
	uint32_t saved_flags;

	wt_session = &session->iface;

	/* First open a new cursor with the same URI. */
	WT_ERR(wt_session->open_cursor(
	    wt_session, to_dup->uri, NULL, config, &cursor));

	/*
	 * If the original cursor is positioned, copy the key and position the
	 * new cursor.  Temporarily force raw mode in both cursors to get a
	 * canonical copy of the key.
	 */
	if (F_ISSET(to_dup, WT_CURSTD_KEY_SET)) {
		/* Get the (raw) key from the original cursor. */
		saved_flags = F_ISSET(to_dup, WT_CURSTD_RAW);
		if (saved_flags == 0)
			F_SET(to_dup, WT_CURSTD_RAW);
		ret = to_dup->get_key(to_dup, &key);
		if (saved_flags == 0)
			F_CLR(to_dup, WT_CURSTD_RAW);
		WT_ERR(ret);

		/* Set the (raw) key in the new cursor. */
		saved_flags = F_ISSET(cursor, WT_CURSTD_RAW);
		if (saved_flags == 0)
			F_SET(cursor, WT_CURSTD_RAW);
		cursor->set_key(cursor, &key);
		if (saved_flags == 0)
			F_CLR(cursor, WT_CURSTD_RAW);
		WT_ERR(cursor->search(cursor));
	}

	if (0) {
err:		if (cursor != NULL)
			(void)cursor->close(cursor);
		cursor = NULL;
	}

	*cursorp = cursor;
	return (ret);
}

/*
 * __wt_cursor_init --
 *	Default cursor initialization.
 *
 *	Most cursors are "public", and added to the list in the session
 *	to be closed when the cursor is closed.  However, some cursors are
 *	opened for internal use, or are opened inside another cursor (such
 *	as column groups or indices within a table cursor), and adding those
 *	cursors to the list introduces ordering dependencies into
 *	WT_SESSION->close that we prefer to avoid.
 */
int
__wt_cursor_init(WT_CURSOR *cursor,
    const char *uri, WT_CURSOR *owner, const char *cfg[], WT_CURSOR **cursorp)
{
	WT_CURSOR *cdump;
	WT_CONFIG_ITEM cval;
	WT_SESSION_IMPL *session;

	session = (WT_SESSION_IMPL *)cursor->session;

	if (cursor->get_key == NULL)
		cursor->get_key = __wt_cursor_get_key;
	if (cursor->get_value == NULL)
		cursor->get_value = __wt_cursor_get_value;
	if (cursor->set_key == NULL)
		cursor->set_key = __wt_cursor_set_key;
	if (cursor->set_value == NULL)
		cursor->set_value = __wt_cursor_set_value;
	if (cursor->equals == NULL)
		cursor->equals = __cursor_equals;
	if (cursor->search == NULL)
		cursor->search = __cursor_search;

	if (cursor->uri == NULL)
		WT_RET(__wt_strdup(session, uri, &cursor->uri));

	WT_CLEAR(cursor->key);
	WT_CLEAR(cursor->value);

	/* The append flag is only relevant to column stores. */
	if (WT_CURSOR_RECNO(cursor)) {
		WT_RET(__wt_config_gets(session, cfg, "append", &cval));
		if (cval.val != 0)
			F_SET(cursor, WT_CURSTD_APPEND);
	}

	WT_RET(__wt_config_gets(session, cfg, "dump", &cval));
	if (cval.len != 0) {
		F_SET(cursor, (__wt_config_strcmp(&cval, "print") == 0) ?
		    WT_CURSTD_DUMP_PRINT : WT_CURSTD_DUMP_HEX);
		WT_RET(__wt_curdump_create(cursor, owner, &cdump));
		owner = cdump;
	} else
		cdump = NULL;

	WT_RET(__wt_config_gets(session, cfg, "overwrite", &cval));
	if (cval.val != 0)
		F_SET(cursor, WT_CURSTD_OVERWRITE);

	WT_RET(__wt_config_gets(session, cfg, "raw", &cval));
	if (cval.val != 0)
		F_SET(cursor, WT_CURSTD_RAW);

	/* Snapshot cursors are read-only. */
	WT_RET(__wt_config_gets(session, cfg, "snapshot", &cval));
	if (cval.len != 0) {
		cursor->insert = (int (*)(WT_CURSOR *))__wt_cursor_notsup;
		cursor->update = (int (*)(WT_CURSOR *))__wt_cursor_notsup;
		cursor->remove = (int (*)(WT_CURSOR *))__wt_cursor_notsup;
	}

	/*
	 * Cursors that are internal to some other cursor (such as file cursors
	 * inside a table cursor) should be closed after the containing cursor.
	 * Arrange for that to happen by putting internal cursors after their
	 * owners on the queue.
	 */
	if (owner != NULL)
		TAILQ_INSERT_AFTER(&session->cursors, owner, cursor, q);
	else
		TAILQ_INSERT_HEAD(&session->cursors, cursor, q);

	F_SET(cursor, WT_CURSTD_OPEN);

	*cursorp = (cdump != NULL) ? cdump : cursor;
	return (0);
}

/*
 * __wt_cursor_kv_not_set --
 *	Standard error message for key/values not set.
 */
int
__wt_cursor_kv_not_set(WT_CURSOR *cursor, int key)
{
	WT_SESSION_IMPL *session;

	session = (WT_SESSION_IMPL *)cursor->session;

	WT_RET_MSG(session,
	    cursor->saved_err == 0 ? EINVAL : cursor->saved_err,
	    "requires %s be set", key ? "key" : "value");
}
